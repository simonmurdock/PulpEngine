
template<typename T>
inline Pair<T>::Pair(T x, T y) :
    x(x),
    y(y)
{
}

template<typename T>
inline Pair<T> Pair<T>::operator+(const Pair<T>& other)
{
    return Pair<T>(x + other.x, y + other.y);
}

template<typename T>
inline Pair<T> Pair<T>::operator-(const Pair<T>& other)
{
    return Pair<T>(x - other.x, y - other.y);
}

template<typename T>
inline void Pair<T>::set(T x, T y)
{
    this->x = x;
    this->y = y;
}
