#pragma once

namespace AssetLib {

    // 简易二进制块
    struct BinaryData {
        void* data = nullptr;
        size_t size = 0;

        BinaryData() = default;
        explicit BinaryData(const void* buf, size_t len) {
            if (len) {
                size = len;
                data = std::malloc(size);
                if (!data) { // 内存分配失败：保持对象处于安全空状态
                    size = 0;
                    data = nullptr;
                    return;
                }
                if (buf) {
                    std::memcpy(data, buf, len);
                }
                else {
                    // buf 为 nullptr 且 size 非 0：将分配的内存全部清零
                    std::memset(data, 0, len);
                }
            }
        }
        BinaryData(const BinaryData& d) {
            if (d.data && d.size) {
                size = d.size;
                data = std::malloc(size);
                std::memcpy(data, d.data, size);
            }
        }
        BinaryData(std::string_view str) {
            size = str.size();
            if (size) {
                data = std::malloc(size);
                std::memcpy(data, str.data(), size);
            }
        }
		BinaryData(std::string&& str) {
			size = str.size();
			if (size) {
				data = std::malloc(size);
				std::memcpy(data, str.data(), size);
			}
		}

        BinaryData(BinaryData&& d) noexcept {
            data = d.data; size = d.size;
            d.data = nullptr; d.size = 0;
        }
        BinaryData(const char* str) {
            if (str) {
                size = std::strlen(str);
                data = std::malloc(size);
                std::memcpy(data, str, size);
            }
        }
        BinaryData& operator=(const char* str) {
            if (data) { std::free(data); data = nullptr; size = 0; }
            if (str) {
                size = std::strlen(str);
                data = std::malloc(size);
                std::memcpy(data, str, size);
            }
            return *this;
        }
        // 拷贝赋值（const&）
        BinaryData& operator=(const BinaryData& rhs) {
            if (this != &rhs) {
                if (data) { std::free(data); data = nullptr; size = 0; }
                if (rhs.data && rhs.size) {
                    size = rhs.size;
                    data = std::malloc(size);
                    std::memcpy(data, rhs.data, size);
                }
                else {
                    size = 0;
                    data = nullptr;
                }
            }
            return *this;
        }

        // 移动赋值（noexcept）
        BinaryData& operator=(BinaryData&& rhs) noexcept {
            if (this != &rhs) {
                if (data) { std::free(data); }
                data = rhs.data;
                size = rhs.size;
                rhs.data = nullptr;
                rhs.size = 0;
            }
            return *this;
        }

        // 修复：避免返回悬空指针（原实现返回临时 std::string 的 c_str）
        operator const char* () const {
            return static_cast<const char*>(data);
        }

        operator bool() const {
            return data != nullptr && size > 0;
        }
        void Clear() {
            if (data) { std::free(data); data = nullptr; size = 0; }
		}
        ~BinaryData() {
            if (data) { std::free(data); data = nullptr; size = 0; }
        }
    };

    // 端序工具：统一网络字节序（big-endian）
    namespace detail {
        inline constexpr bool is_little_endian() {
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
            return true;
#elif defined(_WIN32)
            return true;
#else
            union { uint32_t u; uint8_t b[4]; } v{ 0x01020304 };
            return v.b[0] != 0x01;
#endif
        }
        template <class T, size_t N = sizeof(T)>
        inline std::enable_if_t<std::is_integral_v<T>, T> to_be(T v) {
            if constexpr (N == 1) return v;
            if (!is_little_endian()) return v;
            if constexpr (N == 2) {
                uint16_t x = (uint16_t)v;
                x = (uint16_t)((x << 8) | (x >> 8));
                return (T)x;
            }
            else if constexpr (N == 4) {
                uint32_t x = (uint32_t)v;
                x = ((x & 0x000000FFu) << 24)
                    | ((x & 0x0000FF00u) << 8)
                    | ((x & 0x00FF0000u) >> 8)
                    | ((x & 0xFF000000u) >> 24);
                return (T)x;
            }
            else { // 8
                uint64_t x = (uint64_t)v;
                x = ((x & 0x00000000000000FFull) << 56)
                    | ((x & 0x000000000000FF00ull) << 40)
                    | ((x & 0x0000000000FF0000ull) << 24)
                    | ((x & 0x00000000FF000000ull) << 8)
                    | ((x & 0x000000FF00000000ull) >> 8)
                    | ((x & 0x0000FF0000000000ull) >> 24)
                    | ((x & 0x00FF000000000000ull) >> 40)
                    | ((x & 0xFF00000000000000ull) >> 56);
                return (T)x;
            }
        }
        template <class T, size_t N = sizeof(T)>
        inline std::enable_if_t<std::is_integral_v<T>, T> from_be(T v_be) {
            // big-endian -> host
            if (!is_little_endian()) return v_be;
            return to_be<T, N>(v_be);
        }
        // 浮点转换：按位处理
        template <class F>
        inline std::enable_if_t<std::is_floating_point_v<F>, F> to_be_f(F v) {
            if constexpr (sizeof(F) == 4) {
                uint32_t u; std::memcpy(&u, &v, 4);
                u = to_be<uint32_t>(u);
                F out; std::memcpy(&out, &u, 4);
                return out;
            }
            else { // 8
                uint64_t u; std::memcpy(&u, &v, 8);
                u = to_be<uint64_t>(u);
                F out; std::memcpy(&out, &u, 8);
                return out;
            }
        }
        template <class F>
        inline std::enable_if_t<std::is_floating_point_v<F>, F> from_be_f(F v_be) {
            if (!is_little_endian()) return v_be;
            if constexpr (sizeof(F) == 4) {
                uint32_t u; std::memcpy(&u, &v_be, 4);
                u = from_be<uint32_t>(u);
                F out; std::memcpy(&out, &u, 4);
                return out;
            }
            else {
                uint64_t u; std::memcpy(&u, &v_be, 8);
                u = from_be<uint64_t>(u);
                F out; std::memcpy(&out, &u, 8);
                return out;
            }
        }
    }

    // 检测类型是否有成员函数 void Serialize(SerializerFactory&)
    template <typename T, typename = void>
    struct has_serialize : std::false_type {};

    template <typename T>
    struct has_serialize<T, std::void_t<decltype(std::declval<T&>().Serialize(std::declval<class SerializerFactory&>()))>> : std::true_type {};

    template <typename T>
    inline constexpr bool has_serialize_v = has_serialize<T>::value;

    // type trait: 检测 std::vector<...>
    template <typename X> struct is_std_vector : std::false_type {};
    template <typename A, typename Alloc>
    struct is_std_vector<std::vector<A, Alloc>> : std::true_type {};

    // SerializerFactory：支持统一端序
    class SerializerFactory {
    public:
        SerializerFactory() = default;
        explicit SerializerFactory(const BinaryData& bin) { BeginRead(bin); }

        void BeginWrite() {
            modeSerialize_ = true;
            chunks_.clear();
            readPtr_ = readBegin_ = readEnd_ = nullptr;
        }

        void BeginRead(const BinaryData& bin) {
            modeSerialize_ = false;
            readStorage_.assign((const std::uint8_t*)bin.data,
                (const std::uint8_t*)bin.data + bin.size);
            readBegin_ = (readStorage_.empty() ? nullptr : (char*)readStorage_.data());
            readPtr_ = readBegin_;
            readEnd_ = (readStorage_.empty() ? nullptr : (char*)readStorage_.data() + readStorage_.size());
            chunks_.clear();
        }

        BinaryData ToBinary() const {
            size_t total = 0;
            for (const auto& c : chunks_) total += c.size();
            std::vector<std::uint8_t> out;
            out.reserve(total);
            for (const auto& c : chunks_) out.insert(out.end(), c.begin(), c.end());
            return BinaryData(out.data(), out.size());
        }

        template <class T>
        SerializerFactory& operator%(T& t) { process(t); return *this; }

        template <class T>
        SerializerFactory& operator%(const T& t) {
            if (!modeSerialize_) throw std::logic_error("Read mode requires non-const reference");
            processConst(t);
            return *this;
        }

        // 静态封装
        template <class T, void(T::*)(SerializerFactory&) = &T::Serialize>
        static void Serialize(T& t, BinaryData& out) {
            SerializerFactory sz; sz.BeginWrite(); sz% t; out = sz.ToBinary();
        }
        template <class T, void(T::*)(SerializerFactory&) = &T::Serialize>
        static void Deserialize(T& t, const BinaryData& in) {
            SerializerFactory sz(in); sz% t;
        }

    private:
        bool modeSerialize_ = true;
        std::vector<std::vector<std::uint8_t>> chunks_;
        std::vector<std::uint8_t> readStorage_;
        char* readPtr_ = nullptr;
        char* readBegin_ = nullptr;
        char* readEnd_ = nullptr;

        void addChunk(const void* data, size_t len) {
            if (len == 0) return;
            const auto* p = static_cast<const std::uint8_t*>(data);
            chunks_.emplace_back();
            chunks_.back().assign(p, p + len);
        }
        void copyFromRead(void* d, size_t len) {
            if (!readPtr_ || !readEnd_) throw std::runtime_error("Deserialize: empty buffer");
            if ((size_t)(readEnd_ - readPtr_) < len)
                throw std::runtime_error("Deserialize: buffer underflow");
            std::memcpy(d, readPtr_, len);
            readPtr_ += len;
        }

        // 整数类型（统一端序）
        template <class T>
        std::enable_if_t<std::is_integral_v<T>, void>
            process(T& v) {
            if (modeSerialize_) {
                T be = detail::to_be<T>(v);
                addChunk(&be, sizeof(T));
            }
            else {
                T be{}; copyFromRead(&be, sizeof(T));
                v = detail::from_be<T>(be);
            }
        }
        template <class T>
        std::enable_if_t<std::is_integral_v<T>, void>
            processConst(const T& v) {
            T be = detail::to_be<T>(v);
            addChunk(&be, sizeof(T));
        }

        // 浮点类型（按位端序）
        template <class F>
        std::enable_if_t<std::is_floating_point_v<F>, void>
            process(F& v) {
            if (modeSerialize_) {
                F be = detail::to_be_f<F>(v);
                addChunk(&be, sizeof(F));
            }
            else {
                F be{}; copyFromRead(&be, sizeof(F));
                v = detail::from_be_f<F>(be);
            }
        }
        template <class F>
        std::enable_if_t<std::is_floating_point_v<F>, void>
            processConst(const F& v) {
            F be = detail::to_be_f<F>(v);
            addChunk(&be, sizeof(F));
        }

        // enum
        template <class T>
        std::enable_if_t<std::is_enum_v<T>, void>
            process(T& v) {
            using U = std::underlying_type_t<T>;
            U u = static_cast<U>(v);
            process(u);
            if (!modeSerialize_) v = static_cast<T>(u);
        }
        template <class T>
        std::enable_if_t<std::is_enum_v<T>, void>
            processConst(const T& v) {
            using U = std::underlying_type_t<T>;
            U u = static_cast<U>(v);
            processConst(u);
        }

        // BinaryData（长度使用 BE）
        void process(BinaryData& bd) {
            if (modeSerialize_) {
                std::uint32_t len = (std::uint32_t)bd.size;
                std::uint32_t len_be = detail::to_be<std::uint32_t>(len);
                addChunk(&len_be, sizeof(len_be));
                if (len && bd.data) addChunk(bd.data, len);
            }
            else {
                std::uint32_t len_be = 0;
                copyFromRead(&len_be, sizeof(len_be));
                std::uint32_t len = detail::from_be<std::uint32_t>(len_be);
                BinaryData tmp;
                if (len) {
                    tmp = BinaryData(readPtr_, len);
                    readPtr_ += len;
                }
                bd = std::move(tmp);
            }
        }
        void processConst(const BinaryData& bd) {
            std::uint32_t len = (std::uint32_t)bd.size;
            std::uint32_t len_be = detail::to_be<std::uint32_t>(len);
            addChunk(&len_be, sizeof(len_be));
            if (len && bd.data) addChunk(bd.data, len);
        }

        // std::string（长度使用 BE）
        void process(std::string& s) {
            if (modeSerialize_) {
                std::uint32_t len = (std::uint32_t)s.size();
                std::uint32_t len_be = detail::to_be<std::uint32_t>(len);
                addChunk(&len_be, sizeof(len_be));
                if (len) addChunk(s.data(), len);
            }
            else {
                std::uint32_t len_be = 0;
                copyFromRead(&len_be, sizeof(len_be));
                std::uint32_t len = detail::from_be<std::uint32_t>(len_be);
                s.resize(len);
                if (len) copyFromRead(&s[0], len);
            }
        }
        void processConst(const std::string& s) {
            std::uint32_t len = (std::uint32_t)s.size();
            std::uint32_t len_be = detail::to_be<std::uint32_t>(len);
            addChunk(&len_be, sizeof(len_be));
            if (len) addChunk(s.data(), len);
        }

        // const char*（仅序列化，长度使用 BE，不写入终止符）
        void processConst(const char* s) {
            std::uint32_t len = s ? (std::uint32_t)std::strlen(s) : 0;
            std::uint32_t len_be = detail::to_be<std::uint32_t>(len);
            addChunk(&len_be, sizeof(len_be));
            if (len) addChunk(s, len);
        }

        // std::vector<E>（长度使用 BE）
        template <class E>
        void process(std::vector<E>& vec) {
            if (modeSerialize_) {
                std::uint32_t n = (std::uint32_t)vec.size();
                std::uint32_t n_be = detail::to_be<std::uint32_t>(n);
                addChunk(&n_be, sizeof(n_be));
                for (std::uint32_t i = 0; i < n; ++i) process(vec[i]);
            }
            else {
                std::uint32_t n_be = 0;
                copyFromRead(&n_be, sizeof(n_be));
                std::uint32_t n = detail::from_be<std::uint32_t>(n_be);
                vec.resize(n);
                for (std::uint32_t i = 0; i < n; ++i) process(vec[i]);
            }
        }
        template <class E>
        void processConst(const std::vector<E>& vec) {
            std::uint32_t n = (std::uint32_t)vec.size();
            std::uint32_t n_be = detail::to_be<std::uint32_t>(n);
            addChunk(&n_be, sizeof(n_be));
            for (std::uint32_t i = 0; i < n; ++i) processConst(vec[i]);
        }

        // 用户类型：要求有 void Serialize(SerializerFactory&)
        // 通过 SFINAE 限制，仅当 T 具有 Serialize 且不属于已特化类别时启用，避免与整型/浮点/枚举产生歧义
        template <class T>
        std::enable_if_t<
            has_serialize_v<T> &&
            !std::is_integral_v<T> &&
            !std::is_floating_point_v<T> &&
            !std::is_enum_v<T> &&
            !std::is_same_v<T, BinaryData> &&
            !std::is_same_v<T, std::string> &&
            !is_std_vector<T>::value,
            void>
            process(T& v) { v.Serialize(*this); }

        template <class T>
        std::enable_if_t<
            has_serialize_v<T> &&
            !std::is_integral_v<T> &&
            !std::is_floating_point_v<T> &&
            !std::is_enum_v<T> &&
            !std::is_same_v<T, BinaryData> &&
            !std::is_same_v<T, std::string> &&
            !is_std_vector<T>::value,
            void>
            processConst(const T& v) { const_cast<T&>(v).Serialize(*this); }
    };

}; // namespace AssetLib