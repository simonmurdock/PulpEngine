

X_NAMESPACE_BEGIN(script)

X_INLINE ScriptValue::ScriptValue(bool value) :
    type_(Type::Boolean)
{
    bool_ = value;
}

X_INLINE ScriptValue::ScriptValue(int32_t value) :
    type_(Type::Number)
{
    number_ = static_cast<double>(value);
}

X_INLINE ScriptValue::ScriptValue(uint32_t value) :
    type_(Type::Number)
{
    number_ = static_cast<double>(value);
}

X_INLINE ScriptValue::ScriptValue(float value) :
    type_(Type::Number)
{
    number_ = value;
}

X_INLINE ScriptValue::ScriptValue(double value) :
    type_(Type::Number)
{
    number_ = value;
}

X_INLINE ScriptValue::ScriptValue(const char* pValue) :
    type_(Type::String)
{
    str_.pStr = pValue;
    str_.len = safe_static_cast<int32_t>(::strlen(pValue));
}

X_INLINE ScriptValue::ScriptValue(core::StringRange<char>& value) :
    type_(Type::String)
{
    str_.pStr = value.getStart();
    str_.len = safe_static_cast<int32_t>(value.getLength());
}

X_INLINE ScriptValue::ScriptValue(IScriptTable* table) :
    type_(Type::Table)
{
    pTable_ = table;
    if (pTable_) {
        pTable_->addRef();
    }
}

X_INLINE ScriptValue::ScriptValue(ScriptFunctionHandle function) :
    type_(Type::Function)
{
    pFunction_ = function;
}

X_INLINE ScriptValue::ScriptValue(Handle value) :
    type_(Type::Handle)
{
    pPtr_ = value.pPtr;
}

X_INLINE ScriptValue::ScriptValue(const Vec3f& vec) :
    type_(Type::Vector)
{
    vec3_.x = vec.x;
    vec3_.y = vec.y;
    vec3_.z = vec.z;
}

X_INLINE ScriptValue::ScriptValue(const SmartScriptTable& value) :
    type_(Type::Table)
{
    pTable_ = value;
    if (pTable_) {
        pTable_->addRef();
    }
}

X_INLINE ScriptValue::ScriptValue(Type::Enum type) :
    type_(type),
    pPtr_(nullptr)
{
}

X_INLINE ScriptValue::ScriptValue() :
    type_(Type::None),
    pPtr_(nullptr)
{
}

X_INLINE ScriptValue::~ScriptValue()
{
    clear();
}

X_INLINE void ScriptValue::clear(void)
{
    if (type_ == Type::Table && pTable_) {
        pTable_->release();
    }
    else if (type_ == Type::Function && pFunction_) {
        gEnv->pScriptSys->releaseFunc(pFunction_);
    }

    type_ = Type::None;
}

X_INLINE Type::Enum ScriptValue::getType(void) const
{
    return type_;
}

X_INLINE ScriptValue& ScriptValue::operator=(const ScriptValue& rhs)
{
    memcpy(this, &rhs, sizeof(ScriptValue));
    return *this;
}

X_INLINE bool ScriptValue::operator==(const ScriptValue& rhs) const
{
    bool result = type_ == rhs.type_;
    if (result) {
        switch (type_) {
            case Type::Boolean:
                result = bool_ == rhs.bool_;
                break;
            case Type::Number:
                result = number_ == rhs.number_;
                break;
            case Type::String:
                result = (str_.len == rhs.str_.len && str_.pStr == rhs.str_.pStr);
                break;
            case Type::Vector:
                result = vec3_.x == rhs.vec3_.x && vec3_.y == rhs.vec3_.y && vec3_.z == rhs.vec3_.z;
                break;
            case Type::Table:
                result = pTable_ == rhs.pTable_;
                break;
            case Type::Handle:
                result = pPtr_ == rhs.pPtr_;
                break;
            case Type::Function:
                result = gEnv->pScriptSys->compareFuncRef(pFunction_, rhs.pFunction_);
                break;
                //		case Type::USERDATA: result = ud.nRef == rhs.ud.nRef && ud.ptr == rhs.ud.ptr; break;

            default:
                X_ASSERT_NOT_IMPLEMENTED();
                break;
        }
    }
    return result;
}

X_INLINE bool ScriptValue::operator!=(const ScriptValue& rhs) const
{
    return !(*this == rhs);
}

X_INLINE void ScriptValue::swap(ScriptValue& value)
{
    core::Swap(*this, value);
}

X_INLINE bool ScriptValue::copyTo(bool& value) const
{
    if (type_ == Type::Boolean) {
        value = bool_;
        return true;
    }
    return false;
}

X_INLINE bool ScriptValue::copyTo(int32_t& value) const
{
    if (type_ == Type::Number) {
        value = static_cast<int32_t>(number_);
        return true;
    }
    return false;
}

X_INLINE bool ScriptValue::copyTo(uint32_t& value) const
{
    if (type_ == Type::Number) {
        value = static_cast<uint32_t>(number_);
        return true;
    }
    return false;
}

X_INLINE bool ScriptValue::copyTo(float& value) const
{
    if (type_ == Type::Number) {
        value = static_cast<float>(number_);
        return true;
    }
    return false;
}

X_INLINE bool ScriptValue::copyTo(const char*& value) const
{
    if (type_ == Type::String) {
        value = str_.pStr;
        return true;
    }
    return false;
}

X_INLINE bool ScriptValue::copyTo(char*& value) const
{
    if (type_ == Type::String) {
        value = (char*)str_.pStr;
        return true;
    }
    return false;
}

X_INLINE bool ScriptValue::copyTo(core::StringRange<char>& value) const
{
    if (type_ == Type::String) {
        value = core::StringRange<char>((char*)str_.pStr, (char*)str_.pStr + str_.len);
        return true;
    }
    return false;
}

X_INLINE bool ScriptValue::copyTo(Handle& value) const
{
    if (type_ == Type::Handle) {
        value.pPtr = const_cast<void*>(pPtr_);
        return true;
    }
    return false;
}

X_INLINE bool ScriptValue::copyTo(ScriptFunctionHandle& value) const
{
    X_UNUSED(value);
    X_ASSERT_NOT_IMPLEMENTED();
    return false;
}

X_INLINE bool ScriptValue::copyTo(Vec3f& value) const
{
    if (type_ == Type::Vector) {
        value.x = vec3_.x;
        value.y = vec3_.y;
        value.z = vec3_.z;
        return true;
    }
    return false;
}

X_INLINE bool ScriptValue::copyTo(IScriptTable*& value) const
{
    if (type_ == Type::Table) {
        value = pTable_;
        return true;
    }
    return false;
}

X_INLINE bool ScriptValue::copyTo(SmartScriptTable& value) const
{
    if (type_ == Type::Table) {
        value = pTable_;
        return true;
    }
    return false;
}

// ------------------------------------------------------------------------

template<class T>
X_INLINE void IScriptSys::setGlobalValue(const char* pKey, const T& value)
{
    setGlobalValue(pKey, ScriptValue(value));
}

X_INLINE void IScriptSys::setGlobalToNull(const char* pKey)
{
    setGlobalValue(pKey, ScriptValue(Type::Nil));
}

template<class T>
X_INLINE bool IScriptSys::getGlobalValue(const char* pKey, T& value)
{
    ScriptValue any(ValueType<T>::Type);
    return getGlobalValue(pKey, any) && any.copyTo(value);
}

// ------------------------------------------------------------------------

template<class T>
X_INLINE bool IFunctionHandler::getSelf(T& value)
{
    ScriptValue any(value, 0);
    return getSelfAny(any) && any.copyTo(value);
}

template<typename T>
X_INLINE bool IFunctionHandler::getParam(int32_t idx, T& value)
{
    ScriptValue any(ValueType<T>::Type);
    return getParamAny(idx, any) && any.copyTo(value);
}

template<typename T>
X_INLINE bool IFunctionHandler::getParam(T& value)
{
    return getParam<T>(1, value);
}

template<typename T, typename T2>
X_INLINE bool IFunctionHandler::getParam(T& value1, T2& value2)
{
    return getParam<T>(1, value1) && getParam<T2>(v2, alue2);
}

template<typename T, typename T2, typename T3>
X_INLINE bool IFunctionHandler::getParam(T& value1, T2& value2, T3& value3)
{
    return getParam<T>(1, value1) && getParam<T2>(2, value2) && getParam<T3>(3, value3);
}

template<typename T, typename T2, typename T3, typename T4>
X_INLINE bool IFunctionHandler::getParam(T& value1, T2& value2, T3& value3, T4& value4)
{
    return getParam<T>(1, value1) && getParam<T2>(2, value2) && getParam<T3>(3, value3) && getParam<T4>(4, value4);
}

X_INLINE int32_t IFunctionHandler::endFunction(void) const
{
    return 0;
}

X_INLINE int32_t IFunctionHandler::endFunctionNull(void) const
{
    return 0;
}

template<class T>
X_INLINE int IFunctionHandler::endFunction(const T& value)
{
    return endFunctionAny(value);
}

template<class T1, class T2>
X_INLINE int IFunctionHandler::endFunction(const T1& value1, const T2& value2)
{
    return endFunctionAny(value1, value2);
}

template<class T1, class T2, class T3>
X_INLINE int IFunctionHandler::endFunction(const T1& value1, const T2& value2, const T3& value3)
{
    return endFunctionAny(value1, value2, value3);
}

// ------------------------------------------------------------------------

ScriptFunctionDesc::ScriptFunctionDesc() :
    pFunctionName(""),
    pFunctionParams(""),
    pGlobalName(""),
    paramIdOffset(0),
    userDataSize(0),
    pDataBuffer(nullptr),
    pUserDataFunc(nullptr)
{
}

// ----------------------------------------------------------------

template<class T>
X_INLINE void IScriptTable::pushBack(const T& value)
{
    const int32_t pos = count() + 1;
    setValueAny(pos, value);
}

template<class T>
X_INLINE void IScriptTable::setValue(const char* pKey, const T& value)
{
    return setValueAny(pKey, value);
}

template<class T>
X_INLINE void IScriptTable::setValue(int idx, const T& value)
{
    return setValueAny(idx, value);
}

template<class T>
X_INLINE void IScriptTable::setValueChain(const char* pKey, const T& value)
{
    return setValueAny(pKey, value, true);
}

//	 Gets value of a table member.
template<class T>
X_INLINE bool IScriptTable::getValue(const char* pKey, T& value)
{
    ScriptValue any(ValueType<T>::Type);
    return getValueAny(pKey, any) && any.copyTo(value);
}

template<class T>
X_INLINE bool IScriptTable::getValue(int idx, T& value)
{
    ScriptValue any(ValueType<T>::Type);
    return getValueAny(idx, any) && any.copyTo(value);
}

template<class T>
X_INLINE bool IScriptTable::getValueChain(const char* pKey, T& value)
{
    ScriptValue any(ValueType<T>::Type);
    return getValueAny(pKey, any, true) && any.copyTo(value);
}

X_INLINE bool IScriptTable::haveValue(const char* pKey)
{
    X_UNUSED(pKey);
    X_ASSERT_NOT_IMPLEMENTED();
    return false;
}

X_INLINE bool IScriptTable::haveValue(int idx)
{
    X_UNUSED(idx);
    X_ASSERT_NOT_IMPLEMENTED();
    return false;
}

X_INLINE void IScriptTable::setToNull(const char* pKey)
{
    setValueAny(pKey, ScriptValue(Type::Nil));
}

X_INLINE void IScriptTable::setToNull(int idx)
{
    setValueAny(idx, ScriptValue(Type::Nil));
}

X_INLINE void IScriptTable::setToNullChain(const char* pKey)
{
    setValueAny(pKey, ScriptValue(Type::Nil), true);
}

// ----------------------------------------------------------------

X_INLINE IScriptBindsBase::IScriptBindsBase(IScriptSys* pScriptSys) :
    pScriptSys_(pScriptSys),
    pBindTable_(nullptr)
{
}

X_INLINE IScriptTable* IScriptBindsBase::getMethodsTable(void)
{
    return pBindTable_->getMethodsTable();
}

X_INLINE void IScriptBindsBase::createBindTable(void)
{
    pBindTable_ = pScriptSys_->createScriptBind();
}

X_INLINE void IScriptBindsBase::setGlobalName(const char* pGlobalName)
{
    pBindTable_->setGlobalName(pGlobalName);
}

// ------------------------------------------------------------------------

X_INLINE SmartScriptTable::SmartScriptTable() :
    pTable_(nullptr)
{
}

X_INLINE SmartScriptTable::SmartScriptTable(const SmartScriptTable& st)
{
    pTable_ = st.pTable_;
    if (pTable_) {
        pTable_->addRef();
    }
}

X_INLINE SmartScriptTable::SmartScriptTable::SmartScriptTable(SmartScriptTable&& st)
{
    pTable_ = st.pTable_;
    st.pTable_ = nullptr;
}

X_INLINE SmartScriptTable::SmartScriptTable(IScriptTable* pNew)
{
    if (pNew) {
        pNew->addRef();
    }
    pTable_ = pNew;
}

X_INLINE SmartScriptTable::SmartScriptTable(IScriptSys* pSS, bool createEmpty = false)
{
    pTable_ = pSS->createTable(createEmpty);
}

X_INLINE SmartScriptTable::~SmartScriptTable()
{
    if (pTable_) {
        pTable_->release();
    }
}

// Copy operator.
X_INLINE SmartScriptTable& SmartScriptTable::operator=(IScriptTable* pNew)
{
    if (pNew) {
        pNew->addRef();
    }
    if (pTable_) {
        pTable_->release();
    }
    pTable_ = pNew;
    return *this;
}

// Copy operator.
X_INLINE SmartScriptTable& SmartScriptTable::operator=(const SmartScriptTable& st)
{
    if (st.pTable_) {
        st.pTable_->addRef();
    }
    if (pTable_) {
        pTable_->release();
    }
    pTable_ = st.pTable_;
    return *this;
}

X_INLINE SmartScriptTable& SmartScriptTable::operator=(SmartScriptTable&& st)
{
    if (pTable_) {
        pTable_->release();
    }
    pTable_ = st.pTable_;
    st.pTable_ = nullptr;
    return *this;
}

// Casts
X_INLINE IScriptTable* SmartScriptTable::operator->() const
{
    return pTable_;
}
X_INLINE IScriptTable* SmartScriptTable::operator*() const
{
    return pTable_;
}
X_INLINE SmartScriptTable::operator const IScriptTable*() const
{
    return pTable_;
}
X_INLINE SmartScriptTable::operator IScriptTable*() const
{
    return pTable_;
}
X_INLINE SmartScriptTable::operator bool() const
{
    return pTable_ != nullptr;
}

// Boolean comparasions.
X_INLINE bool SmartScriptTable::operator!() const
{
    return pTable_ == nullptr;
}

X_INLINE bool SmartScriptTable::operator==(const IScriptTable* p2) const
{
    return pTable_ == p2;
}

X_INLINE bool SmartScriptTable::operator==(IScriptTable* p2) const
{
    return pTable_ == p2;
}

X_INLINE bool SmartScriptTable::operator!=(const IScriptTable* p2) const
{
    return pTable_ != p2;
}

X_INLINE bool SmartScriptTable::operator!=(IScriptTable* p2) const
{
    return pTable_ != p2;
}

X_INLINE bool SmartScriptTable::operator<(const IScriptTable* p2) const
{
    return pTable_ < p2;
}

X_INLINE bool SmartScriptTable::operator>(const IScriptTable* p2) const
{
    return pTable_ > p2;
}

X_INLINE IScriptTable* SmartScriptTable::getPtr(void) const
{
    return pTable_;
}

X_INLINE bool SmartScriptTable::create(IScriptSys* pSS, bool createEmpty = false)
{
    if (pTable_) {
        pTable_->release();
    }
    pTable_ = pSS->createTable(createEmpty);
    return pTable_ != nullptr;
}

// ------------------------------------------------------------------------

X_INLINE SmartScriptFunction::SmartScriptFunction() :
    func_(INVALID_HANLDE),
    pSS_(nullptr)
{
}

X_INLINE SmartScriptFunction::SmartScriptFunction(IScriptSys* pSS, ScriptFunctionHandle func) :
    func_(func),
    pSS_(pSS)
{
}

X_INLINE SmartScriptFunction::SmartScriptFunction(SmartScriptFunction&& oth) :
    func_(oth.func_),
    pSS_(oth.pSS_)
{
    oth.func_ = INVALID_HANLDE;
    oth.pSS_ = nullptr;
}

X_INLINE SmartScriptFunction::~SmartScriptFunction()
{
    if (func_ != INVALID_HANLDE) {
        pSS_->releaseFunc(func_);
    }
}

X_INLINE SmartScriptFunction& SmartScriptFunction::operator=(SmartScriptFunction&& oth)
{
    func_ = oth.func_;
    pSS_ = oth.pSS_;

    oth.func_ = INVALID_HANLDE;
    oth.pSS_ = nullptr;
    return *this;
}

X_INLINE SmartScriptFunction::operator ScriptFunctionHandle() const
{
    return func_;
}

X_INLINE ScriptFunctionHandle SmartScriptFunction::get(void) const
{
    return func_;
}

X_INLINE void SmartScriptFunction::swap(SmartScriptFunction& oth)
{
    core::Swap(func_, oth.func_);
    core::Swap(pSS_, oth.pSS_);
}

X_INLINE void SmartScriptFunction::reset(void)
{
    SmartScriptFunction().swap(*this);
}

X_INLINE void SmartScriptFunction::reset(IScriptSys* pSS, ScriptFunctionHandle func)
{
    SmartScriptFunction(pSS, func).swap(*this);
}

X_NAMESPACE_END
