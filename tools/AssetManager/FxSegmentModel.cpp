#include "stdafx.h"
#include "FxSegmentModel.h"

#include <String\Json.h>
#include <Hashing\Fnva1Hash.h>

#include <../FxLib/FxLib.h>

using namespace engine::fx;
using namespace core::Hash::Literals;

X_NAMESPACE_BEGIN(assman)

// -----------------------------------

namespace
{
	auto checkMember = [](const auto& d, const char* pName, core::json::Type type) -> bool {

		if (!d.HasMember(pName)) {
			X_ERROR("Fx", "Missing param: \"%s\"", pName);
			return false;
		}

		auto srcType = d[pName].GetType();
		if (srcType == type) {
			return true;
		}

		X_ERROR("Fx", "Param \"%s\" has incorrect type", pName);
		return false;
	};



} // namespace

FxSegmentModel::FxSegmentModel(QObject *parent) :
	QAbstractTableModel(parent)
{

}

void FxSegmentModel::getJson(core::string& jsonStrOut) const
{
	// build me some json.
	typedef core::json::Writer<core::json::StringBuffer> JsonWriter;

	core::json::StringBuffer s;
	JsonWriter writer(s);

	writer.SetMaxDecimalPlaces(8);

	auto writeRange = [](JsonWriter& writer, const char* pPrefix, const Range& range) {

		core::StackString<64> startStr(pPrefix);
		core::StackString<64> rangeStr(pPrefix);

		startStr.append("Start");
		rangeStr.append("Range");

		writer.Key(startStr.c_str());
		writer.Int(range.start);
		writer.Key(rangeStr.c_str());
		writer.Int(range.range);
	};

	auto writeRangeDouble = [](JsonWriter& writer, const char* pPrefix, const RangeDouble& range) {

		core::StackString<64> startStr(pPrefix);
		core::StackString<64> rangeStr(pPrefix);

		startStr.append("Start");
		rangeStr.append("Range");

		writer.Key(startStr.c_str());
		writer.Double(range.start);
		writer.Key(rangeStr.c_str());
		writer.Double(range.range);
	};

	auto writeGraph = [](JsonWriter& writer, const char* pName, const GraphInfo& g, float scale) {

		writer.Key(pName);
		writer.StartObject();
		writer.Key("scale");
		writer.Double(scale);

		writer.Key("graphs");
		writer.StartArray();

		for (size_t i = 0; i < g.graphs.size(); i++)
		{
			const auto& graph = g.graphs[i];

			writer.StartArray();

			X_ASSERT(graph.series.size() == 1, "Unexpected series size")(graph.series.size());

			auto& series = graph.series.front();

			for (size_t p = 0; p < series.points.size(); p++)
			{
				auto& point = series.points[p];

				writer.StartObject();
				writer.Key("time");
				writer.Double(point.pos);
				writer.Key("val");
				writer.Double(point.val);
				writer.EndObject();
			}

			writer.EndArray();
		}

		writer.EndArray();
		writer.EndObject();
	};

	auto writeSubGraph = [](JsonWriter& writer, const char* pName, const GraphInfo& g, float scale, std::initializer_list<size_t> indexes) {

		writer.Key(pName);
		writer.StartObject();
		writer.Key("scale");
		writer.Double(scale);

		writer.Key("graphs");
		writer.StartArray();

		for (size_t i = 0; i < g.graphs.size(); i++)
		{
			if (std::find(indexes.begin(), indexes.end(), i) == indexes.end()) {
				continue;
			}

			const auto& graph = g.graphs[i];

			writer.StartArray();

			X_ASSERT(graph.series.size() == 1, "Unexpected series size")(graph.series.size());

			auto& series = graph.series.front();

			for (size_t p = 0; p < series.points.size(); p++)
			{
				auto& point = series.points[p];

				writer.StartObject();
				writer.Key("time");
				writer.Double(point.pos);
				writer.Key("val");
				writer.Double(point.val);
				writer.EndObject();
			}

			writer.EndArray();
		}

		writer.EndArray();
		writer.EndObject();
	};


	auto writeColGraph = [](JsonWriter& writer, const char* pName, const GraphInfo& g) {

		writer.Key(pName);
		writer.StartObject();
		writer.Key("scale");
		writer.Double(1.0);

		writer.Key("graphs");
		writer.StartArray();

		for (size_t i = 0; i < g.graphs.size(); i++)
		{
			const auto& graph = g.graphs[i];

			writer.StartArray();

			X_ASSERT(graph.series.size() == 3, "Unexpected series size")(graph.series.size());

			// all series should be same size.
			auto& seriesR = graph.series[0];
			auto& seriesG = graph.series[1];
			auto& seriesB = graph.series[2];

			bool sizeMatch = (std::adjacent_find(graph.series.begin(), graph.series.end(),
				[](const SeriesData& a, const SeriesData& b) -> bool {
				return a.points.size() != b.points.size();
			}
			) == graph.series.end());

			X_ASSERT(sizeMatch, "Series size don't math")(seriesR.points.size(), seriesG.points.size(), seriesB.points.size());

			for (size_t p = 0; p < seriesR.points.size(); p++)
			{
				// all points sshuld have same pos.
				auto& point = seriesR.points[p];

				auto r = seriesR.points[p].val;
				auto g = seriesG.points[p].val;
				auto b = seriesB.points[p].val;

				writer.StartObject();
				writer.Key("time");
				writer.Double(point.pos);
				writer.Key("rgb");
				writer.StartArray();
				writer.Double(r);
				writer.Double(g);
				writer.Double(b);
				writer.EndArray();
				writer.EndObject();
			}

			writer.EndArray();
		}

		writer.EndArray();
		writer.EndObject();
	};

	writer.StartObject();
	writer.Key("stages");
	writer.StartArray();

	for (auto& segment : segments_)
	{
		writer.StartObject();

		auto name = segment->name.toStdString();

		engine::fx::StageFlags flags;

		switch (segment->origin.offsetType)
		{
			case OriginInfo::OffsetType::Cylindrical:
				flags.Set(StageFlag::SpawnCylindrical);
				break;
			case OriginInfo::OffsetType::Spherical:
				flags.Set(StageFlag::SpawnSphere);
				break;
			default:
				break;
		}

		if (segment->spawn.looping)
		{
			flags.Set(StageFlag::Looping);
		}

		if (segment->origin.relative)
		{
			flags.Set(StageFlag::RelativeOrigin);
		}

		// Color
		if (segment->col.col.random)
		{
			flags.Set(StageFlag::RandGraphCol);
		}
		if (segment->col.alpha.random)
		{
			flags.Set(StageFlag::RandGraphAlpha);
		}

		// size
		if (segment->size.size0.random)
		{
			flags.Set(StageFlag::RandGraphSize0);
		}
		if (segment->size.size1.random)
		{
			flags.Set(StageFlag::RandGraphSize1);
		}
		if (segment->size.size2Enabled)
		{
			flags.Set(StageFlag::NonUniformScale);
		}

		// Rot
		if (segment->rot.rot.random)
		{
			flags.Set(StageFlag::RandGraphRot);
		}
		
		// Vel
		if (segment->vel.vel0.graph.random)
		{
			flags.Set(StageFlag::RandGraphVel0);
		}
		if (segment->vel.vel1.graph.random)
		{
			flags.Set(StageFlag::RandGraphVel1);
		}
		if (segment->vel.vel0.relative)
		{
			flags.Set(StageFlag::RelativeVel0);
		}
		if (segment->vel.vel1.relative)
		{
			flags.Set(StageFlag::RelativeVel1);
		}

		// manually build flag string instead of using Flag::ToString, as the format of the flags is important.
		engine::fx::Util::FlagStr flagsStr;
		engine::fx::Util::FlagStrFromFlags(flags, flagsStr);

		writer.Key("name");
		writer.String(name.c_str());

		writer.Key("enabled");
		writer.Bool(segment->enabled);

		writer.Key("type");
		writer.String(StageType::ToString(segment->vis.type));

		writer.Key("relativeTo");
		writer.String(RelativeTo::ToString(segment->vel.postionType));

		writer.Key("flags");
		writer.String(flagsStr.c_str());

		auto writeStringArr = [&writer](const char* pKey, const std::vector<QString>& strArr) {
			writer.Key(pKey);
			writer.StartArray();
			for (auto mat : strArr)
			{
				auto matStd = mat.toStdString();
				writer.String(matStd.c_str());
			}
			writer.EndArray();
		};

		writeStringArr("materials", segment->vis.materials);
		writeStringArr("effects", segment->vis.effects);
		writeStringArr("models", segment->vis.models);
		writeStringArr("sounds", segment->vis.sounds);
		

		writer.Key("interval");
		writer.Int(segment->spawn.interval);
		writer.Key("loopCount");
		writer.Int(segment->spawn.loopCount);

		writeRange(writer, "count", segment->spawn.count);
		writeRange(writer, "life", segment->spawn.life);
		writeRange(writer, "delay", segment->spawn.delay);

		writeRangeDouble(writer, "initialRot", segment->rot.initial);
		writeRangeDouble(writer, "anglePitch", segment->rot.pitch);
		writeRangeDouble(writer, "angleYaw", segment->rot.yaw);
		writeRangeDouble(writer, "angleRoll", segment->rot.roll);

		writeRangeDouble(writer, "spawnOrgX", segment->origin.spawnOrgX);
		writeRangeDouble(writer, "spawnOrgY", segment->origin.spawnOrgY);
		writeRangeDouble(writer, "spawnOrgZ", segment->origin.spawnOrgZ);
		writeRangeDouble(writer, "spawnRadius", segment->origin.spawnRadius);
		writeRangeDouble(writer, "spawnHeight", segment->origin.spawnHeight);

		writer.Key("sequence");
		writer.StartObject();

		writer.Key("startFrame");
		writer.Int(segment->seq.startFrame);
		writer.Key("fps");
		writer.Int(segment->seq.fps);
		writer.Key("loop");
		writer.Int(segment->seq.loop);

		writer.EndObject();


		// GRAPH ME BABY!
		writeColGraph(writer, "colorGraph", segment->col.col);
		writeGraph(writer, "alphaGraph", segment->col.alpha, 1.f);
		writeGraph(writer, "size0Graph", segment->size.size0, segment->size.size0.scale);
		writeGraph(writer, "size1Graph", segment->size.size1, segment->size.size1.scale);
		writeGraph(writer, "scaleGraph", segment->size.scale, segment->size.scale.scale);
		writeGraph(writer, "rotGraph", segment->rot.rot, segment->rot.rot.scale);

		// need to wrtie a verlocity graph.
		// it's a little special since we allow seperate scales.
		// vel0XGraph, vel0XGraph, vel0XGraph is the first graph.
		writeSubGraph(writer, "vel0XGraph", segment->vel.vel0.graph, segment->vel.vel0.forwardScale, { 0,3 });
		writeSubGraph(writer, "vel0YGraph", segment->vel.vel0.graph, segment->vel.vel0.rightScale, { 1,4 });
		writeSubGraph(writer, "vel0ZGraph", segment->vel.vel0.graph, segment->vel.vel0.upScale, { 2,5 });
		writeSubGraph(writer, "vel1XGraph", segment->vel.vel1.graph, segment->vel.vel1.forwardScale, { 0,3 });
		writeSubGraph(writer, "vel1YGraph", segment->vel.vel1.graph, segment->vel.vel1.rightScale, { 1,4 });
		writeSubGraph(writer, "vel1ZGraph", segment->vel.vel1.graph, segment->vel.vel1.upScale, { 2,5 });


		writer.EndObject();
	}

	writer.EndArray();
	writer.EndObject();

	jsonStrOut.assign(s.GetString(), s.GetSize());
}


bool FxSegmentModel::fromJson(const core::string& jsonStr)
{
	// oh my days, this is going to be fun!
	segments_.clear();

	if (jsonStr.isEmpty()) {
		return true;
	}

	core::json::Document d;
	if (d.Parse(jsonStr.c_str(), jsonStr.length()).HasParseError()) {
		core::json::Description dsc;
		X_ERROR("Fx", "Failed to parse fx: %s", core::json::ErrorToString(d, jsonStr.c_str(), jsonStr.c_str() + jsonStr.length(), dsc));
		return false;
	}
	
	if (!d.IsObject()) {
		X_ERROR("Fx", "Stages is not a object. Type: %" PRIi32, d.GetType());
		return false;
	}

	// empty.
	if (d.ObjectEmpty()) {
		return true;
	}

	if (!checkMember(d, "stages", core::json::kArrayType)) {
		return false;
	}
	
	auto& stages = d["stages"];

	for (auto& s : stages.GetArray())
	{
		auto seg = std::make_unique<Segment>();

		if (!checkMember(s, "type", core::json::kStringType)) {
			return false;
		}
		if (!checkMember(s, "relativeTo", core::json::kStringType)) {
			return false;
		}
		if (!checkMember(s, "flags", core::json::kStringType)) {
			return false;
		}
		if (!checkMember(s, "materials", core::json::kArrayType)) {
			return false;
		}
		if (!checkMember(s, "interval", core::json::kNumberType)) {
			return false;
		}
		if (!checkMember(s, "loopCount", core::json::kNumberType)) {
			return false;
		}
		if (!checkMember(s, "sequence", core::json::kObjectType)) {
			return false;
		}

		if (s.HasMember("name") && s["name"].IsString())
		{
			auto& name = s["name"];

			QLatin1String str(name.GetString(), name.GetStringLength());
			seg->name = str;
		}
		else
		{
			seg->name = QString("segment %1").arg(segments_.size());
		}

		if (s.HasMember("enabled") && s["enabled"].IsBool())
		{
			seg->enabled = s["enabled"].GetBool();
		}
		else
		{
			seg->enabled = true;
		}

		{
			auto& seqJson = s["sequence"];

			seg->seq.startFrame = seqJson["startFrame"].GetInt();
			seg->seq.fps = seqJson["fps"].GetInt();
			seg->seq.loop = seqJson["loop"].GetInt();
		}

		auto& typeJson = s["type"];
		auto& relativeToJson = s["relativeTo"];

		seg->vis.type = Util::TypeFromStr(typeJson.GetString(), typeJson.GetString() + typeJson.GetStringLength());
		seg->vel.postionType = Util::RelativeToFromStr(relativeToJson.GetString(), relativeToJson.GetString() + relativeToJson.GetStringLength());

		{
			auto& flagsJson = s["flags"];

			auto flags = engine::fx::Util::FlagsFromStr(flagsJson.GetString(), flagsJson.GetString() + flagsJson.GetStringLength());

			if (flags.IsSet(StageFlag::SpawnCylindrical))
			{
				seg->origin.offsetType = OriginInfo::OffsetType::Cylindrical;
			}
			else if (flags.IsSet(StageFlag::SpawnSphere))
			{
				seg->origin.offsetType = OriginInfo::OffsetType::Spherical;
			}
			else
			{
				seg->origin.offsetType = OriginInfo::OffsetType::None;
			}

			seg->spawn.looping = flags.IsSet(StageFlag::Looping);
			seg->origin.relative = flags.IsSet(StageFlag::RelativeOrigin);
			seg->col.col.random = flags.IsSet(StageFlag::RandGraphCol);
			seg->col.alpha.random = flags.IsSet(StageFlag::RandGraphAlpha);
			seg->size.size0.random = flags.IsSet(StageFlag::RandGraphSize0);
			seg->size.size1.random = flags.IsSet(StageFlag::RandGraphSize1);
			seg->rot.rot.random = flags.IsSet(StageFlag::RandGraphRot);
			seg->vel.vel0.graph.random = flags.IsSet(StageFlag::RandGraphVel0);
			seg->vel.vel1.graph.random = flags.IsSet(StageFlag::RandGraphVel1);

			seg->vel.vel0.relative = flags.IsSet(StageFlag::RelativeVel0);
			seg->vel.vel1.relative = flags.IsSet(StageFlag::RelativeVel1);

			seg->size.size2Enabled = flags.IsSet(StageFlag::NonUniformScale);
		}

		{
			auto& matsJson = s["materials"];

			for (auto& m : matsJson.GetArray())
			{
				if (!m.IsString()) {
					X_ERROR("Fx", "material entry is not type string");
					return false;
				}

				QLatin1String str(m.GetString(), m.GetStringLength());
				seg->vis.materials.emplace_back(QString(str));
			}
		}

		seg->spawn.interval = s["interval"].GetInt();
		seg->spawn.loopCount = s["loopCount"].GetInt();

		const std::array<std::pair<const char*, Range&>, 3> ranges = { {
			{ "count", seg->spawn.count },
			{ "life", seg->spawn.life },
			{ "delay", seg->spawn.delay },
		} };

		const std::array<std::pair<const char*, RangeDouble&>, 9> rangesDouble = { {
			{ "initialRot", seg->rot.initial },
			{ "anglePitch", seg->rot.pitch },
			{ "angleYaw", seg->rot.yaw },
			{ "angleRoll", seg->rot.roll},
			{ "spawnOrgX", seg->origin.spawnOrgX },
			{ "spawnOrgY", seg->origin.spawnOrgY },
			{ "spawnOrgZ", seg->origin.spawnOrgZ },
			{ "spawnRadius", seg->origin.spawnRadius },
			{ "spawnHeight", seg->origin.spawnHeight },
		} };

		for (auto& r : ranges)
		{
			core::StackString<128> start, range;

			start.setFmt("%sStart", r.first);
			range.setFmt("%sRange", r.first);

			if (!s.HasMember(start.c_str()) || !s.HasMember(range.c_str())) {
				X_WARNING("Fx", "Missing required range values: \"%s\" \"%s\"", start.c_str(), range.c_str());
				continue;
			}

			if (!s[start.c_str()].IsNumber() || !s[range.c_str()].IsNumber()) {
				X_ERROR("Fx", "Incorrect type for range values: \"%s\" \"%s\"", start.c_str(), range.c_str());
				return false;
			}

			r.second.start = s[start.c_str()].GetInt();
			r.second.range = s[range.c_str()].GetInt();
		}

		for (auto& r : rangesDouble)
		{
			core::StackString<128> start, range;

			start.setFmt("%sStart", r.first);
			range.setFmt("%sRange", r.first);

			if (!s.HasMember(start.c_str()) || !s.HasMember(range.c_str())) {
				X_WARNING("Fx", "Missing required range values: \"%s\" \"%s\"", start.c_str(), range.c_str());
				continue;
			}

			if (!s[start.c_str()].IsNumber() || !s[range.c_str()].IsNumber()) {
				X_ERROR("Fx", "Incorrect type for range values: \"%s\" \"%s\"", start.c_str(), range.c_str());
				return false;
			}

			r.second.start = s[start.c_str()].GetDouble();
			r.second.range = s[range.c_str()].GetDouble();
		}

		auto readGraph = [](core::json::Document::ValueType& d, const char* pName, GraphInfo& graphInfo, bool optional = false) -> bool {

			if (!checkMember(d, pName, core::json::kObjectType)) {
				return optional;
			}

			auto& graph = d[pName];

			// you silly gost!
			// have scale and graphs.
			if (!checkMember(graph, "scale", core::json::kNumberType)) {
				return false;
			}
			if (!checkMember(graph, "graphs", core::json::kArrayType)) {
				return false;
			}

			graphInfo.scale = graph["scale"].GetFloat();

			for (auto& graph : graph["graphs"].GetArray())
			{
				// each graph is a array of objects
				if (!graph.IsArray()) {
					X_ERROR("Fx", "Graph is not of type array");
					return false;
				}

				GraphData data;
				SeriesData series;

				for (auto& g : graph.GetArray())
				{
					if (!g.IsObject()) {
						X_ERROR("Fx", "Graph member is not a object");
						return false;
					}

					if (!checkMember(g, "time", core::json::kNumberType)) {
						return false;
					}
					if (!checkMember(g, "val", core::json::kNumberType)) {
						return false;
					}

					auto time = g["time"].GetFloat();
					auto val = g["val"].GetFloat();

					series.points.emplace_back(time, val);
				}

				data.series.push_back(std::move(series));
				graphInfo.graphs.push_back(std::move(data));
			}

			return true;
		};

		auto readColGraph = [](core::json::Document::ValueType& d, const char* pName, GraphInfo& graphInfo) -> bool {

			if (!checkMember(d, pName, core::json::kObjectType)) {
				return false;
			}

			auto& graph = d[pName];

			if (!checkMember(graph, "scale", core::json::kNumberType)) {
				return false;
			}
			if (!checkMember(graph, "graphs", core::json::kArrayType)) {
				return false;
			}

			graphInfo.scale = graph["scale"].GetFloat();

			for (auto& graph : graph["graphs"].GetArray())
			{
				// each graph is a array of objects
				if (!graph.IsArray()) {
					X_ERROR("Fx", "Graph is not of type array");
					return false;
				}

				GraphData data;
				data.series.resize(3);

				for (auto& g : graph.GetArray())
				{
					if (!g.IsObject()) {
						X_ERROR("Fx", "Graph member is not a object");
						return false;
					}

					if (!checkMember(g, "time", core::json::kNumberType)) {
						return false;
					}
					if (!checkMember(g, "rgb", core::json::kArrayType)) {
						return false;
					}

					auto time = g["time"].GetFloat();

					auto rgb = g["rgb"].GetArray();
					if (rgb.Size() != 3) {
						return false;
					}

					float col[3];
					col[0] = rgb[0].GetFloat();
					col[1] = rgb[1].GetFloat();
					col[2] = rgb[2].GetFloat();


					data.series[0].points.emplace_back(time, col[0]);
					data.series[1].points.emplace_back(time, col[1]);
					data.series[2].points.emplace_back(time, col[2]);
				}

				graphInfo.graphs.push_back(std::move(data));
			}

			return true;
		};


		bool ok = true;

		// parse me the colors me fat camel of doom!
		ok &= readColGraph(s, "colorGraph", seg->col.col);

		// need to parse graphs.
		ok &= readGraph(s, "alphaGraph", seg->col.alpha);
		ok &= readGraph(s, "size0Graph", seg->size.size0);
		ok &= readGraph(s, "size1Graph", seg->size.size1);
		ok &= readGraph(s, "scaleGraph", seg->size.scale);
		ok &= readGraph(s, "rotGraph", seg->rot.rot);

		GraphInfo vel0XGraph;
		GraphInfo vel0YGraph;
		GraphInfo vel0ZGraph;
		GraphInfo vel1XGraph;
		GraphInfo vel1YGraph;
		GraphInfo vel1ZGraph;

		auto& vel = seg->vel;

		ok &= readGraph(s, "vel0XGraph", vel0XGraph);
		ok &= readGraph(s, "vel0YGraph", vel0YGraph);
		ok &= readGraph(s, "vel0ZGraph", vel0ZGraph);
		ok &= readGraph(s, "vel1XGraph", vel1XGraph);
		ok &= readGraph(s, "vel1YGraph", vel1YGraph);
		ok &= readGraph(s, "vel1ZGraph", vel1ZGraph);

		if (!ok) {
			X_ERROR("Fx", "Failed to read graphs");
			return false;
		}

		vel.vel0.forwardScale = vel0XGraph.scale;
		vel.vel0.rightScale = vel0YGraph.scale;
		vel.vel0.upScale = vel0ZGraph.scale;
		vel.vel0.graph.graphs.resize(6);
		vel.vel0.graph.graphs[0] = std::move(vel0XGraph.graphs[0]);
		vel.vel0.graph.graphs[3] = std::move(vel0XGraph.graphs[1]);
		vel.vel0.graph.graphs[1] = std::move(vel0YGraph.graphs[0]);
		vel.vel0.graph.graphs[4] = std::move(vel0YGraph.graphs[1]);
		vel.vel0.graph.graphs[2] = std::move(vel0ZGraph.graphs[0]);
		vel.vel0.graph.graphs[5] = std::move(vel0ZGraph.graphs[1]);

		vel.vel1.forwardScale = vel1XGraph.scale;
		vel.vel1.rightScale = vel1YGraph.scale;
		vel.vel1.upScale = vel1ZGraph.scale;
		vel.vel1.graph.graphs.resize(6);
		vel.vel1.graph.graphs[0] = std::move(vel1XGraph.graphs[0]);
		vel.vel1.graph.graphs[3] = std::move(vel1XGraph.graphs[1]);
		vel.vel1.graph.graphs[1] = std::move(vel1YGraph.graphs[0]);
		vel.vel1.graph.graphs[4] = std::move(vel1YGraph.graphs[1]);
		vel.vel1.graph.graphs[2] = std::move(vel1ZGraph.graphs[0]);
		vel.vel1.graph.graphs[5] = std::move(vel1ZGraph.graphs[1]);

		segments_.push_back(std::move(seg));
	}

	return true;
}

void FxSegmentModel::addSegment(void)
{
	int currentRows = static_cast<int32_t>(segments_.size());

	beginInsertRows(QModelIndex(), currentRows, currentRows);

	auto seg = std::make_unique<Segment>();

	seg->name = QString("segment %1").arg(segments_.size());
	seg->enabled = true;

	// so for size i want both graphs to have some default points.
	GraphInfo linDescend;
	GraphInfo zero;

	GraphData linGraph;
	GraphData zeroGraph;

	SeriesData linDescendSeries;
	SeriesData zeroSeries;

	{
		linDescendSeries.points.push_back(GraphPoint(0.f, 1.f));
		linDescendSeries.points.push_back(GraphPoint(1.f, 0.f));

		linGraph.series.push_back(linDescendSeries);

		// each graph have one series.
		linDescend.graphs.push_back(linGraph);
		linDescend.graphs.push_back(linGraph);
	}

	{
		zeroSeries.points.push_back(GraphPoint(0.f, 0.f));
		zeroSeries.points.push_back(GraphPoint(1.f, 0.f));

		zeroGraph.series.push_back(zeroSeries);

		zero.graphs.push_back(zeroGraph);
		zero.graphs.push_back(zeroGraph);
	}

	// verlocity has 6 graphs with one seriex.
	seg->vel.vel0.graph.graphs.reserve(6);
	seg->vel.vel1.graph.graphs.reserve(6);
	for (int32_t i = 0; i < 6; i++)
	{
		seg->vel.vel0.graph.graphs.push_back(zeroGraph);
		seg->vel.vel1.graph.graphs.push_back(zeroGraph);
	}

	seg->size.size0.graphs = linDescend.graphs;
	seg->size.size1.graphs = linDescend.graphs;
	seg->size.scale.graphs = linDescend.graphs;
	seg->size.scale.scale = 1.f;

	// so i want 2 graphs with 3 series each.
	{
		GraphData colGraph;
		colGraph.series.push_back(linDescendSeries);
		colGraph.series.push_back(linDescendSeries);
		colGraph.series.push_back(linDescendSeries);

		seg->col.col.graphs.push_back(colGraph);
		seg->col.col.graphs.push_back(std::move(colGraph));
	}

	seg->col.alpha.graphs = linDescend.graphs;

	// rotation is 0.5 - -0.5
	seg->rot.rot.graphs = zero.graphs;

	segments_.push_back(std::move(seg));

	endInsertRows();
}

void FxSegmentModel::duplicateSegment(size_t idx)
{
	X_ASSERT(idx < segments_.size(), "Index out of range")(idx, segments_.size());

	int currentRows = static_cast<int32_t>(segments_.size());

	auto seg = std::make_unique<Segment>(*segments_[idx].get());
	
	beginInsertRows(QModelIndex(), currentRows, currentRows);

	segments_.push_back(std::move(seg));

	endInsertRows();
}

size_t FxSegmentModel::numSegments(void) const
{
	return segments_.size();
}

Segment& FxSegmentModel::getSegment(size_t idx)
{
	return *segments_[idx].get();
}

void FxSegmentModel::setSegmentType(size_t idx, engine::fx::StageType::Enum type)
{
	segments_[idx]->vis.type = type;

	auto modelIndex = index(static_cast<int32_t>(idx), 1);

	emit dataChanged(modelIndex, modelIndex);
}

int FxSegmentModel::rowCount(const QModelIndex & /*parent*/) const
{
	return static_cast<int32_t>(segments_.size());
}

int FxSegmentModel::columnCount(const QModelIndex & /*parent*/) const
{
	return 4;
}

QVariant FxSegmentModel::data(const QModelIndex &index, int role) const
{
	int row = index.row();
	int col = index.column();

	auto& segment = segments_[row];

	switch (role)
	{
		case Qt::DisplayRole:
			if (col == 0)
			{
				return segment->name;
			}
			if (col == 1)
			{
				return engine::fx::StageType::ToString(segment->vis.type);
			}
			if (col == 2)
			{
				return 0;
			}
			if (col == 3)
			{
				return 0;
			}
			break;
		case Qt::CheckStateRole:
			if (col == 0)
			{
				if (segment->enabled) {
					return Qt::Checked;
				}

				return Qt::Unchecked;
			}
	}
	return QVariant();
}

bool FxSegmentModel::setData(const QModelIndex & index, const QVariant & value, int role)
{
	auto& segment = segments_[index.row()];

	if (role == Qt::CheckStateRole && index.column() == 0)
	{
		bool enabled = (value == Qt::Checked);

		if (enabled != segment->enabled)
		{
			segment->enabled = enabled;
			emit dataChanged(index, index);
		}
	}
	else if (role == Qt::EditRole && index.column() == 0)
	{
		if (segment->name != value.toString())
		{
			segment->name = value.toString();
			emit dataChanged(index, index);
		}
	}

	return true;
}

Qt::ItemFlags FxSegmentModel::flags(const QModelIndex &index) const
{
	auto flags = QAbstractTableModel::flags(index);

	if (index.column() == 0) {
		flags |= Qt::ItemIsUserCheckable | Qt::ItemIsEditable;
	}

	return flags;
}

QVariant FxSegmentModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (role == Qt::DisplayRole)
	{
		if (orientation == Qt::Horizontal)
		{
			switch (section)
			{
				case 0:
					return QString("Name");
				case 1:
					return QString("Type");
				case 2:
					return QString("Delay");
				case 3:
					return QString("Count");
			}
		}
	}
	return QVariant();
}

bool FxSegmentModel::removeRows(int row, int count, const QModelIndex &parent)
{
	if (row > segments_.size())
	{
		return false;
	}

	if (count != 1)
	{
		return false;
	}

	beginRemoveRows(parent, row, row);
	segments_.erase(segments_.begin() + row);
	endRemoveRows();
	return true;
}


X_NAMESPACE_END