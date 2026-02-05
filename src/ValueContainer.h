#ifndef VALUECONTAINER_H
#define VALUECONTAINER_H

#include <functional>

template<typename T>
struct ValueContainer{
    private:
        T val{};
        std::function<bool(T,T)> on_change;
        bool empty = true;

    public:

        ValueContainer() = default;

        ValueContainer(const T& v){
            val = v;
            empty = false;
        }

        void set(const T& v) {
            if(!empty && v == val) return;

            if(on_change){
                if(empty || on_change(val, v)){
                    val = v;
                    empty = false;
                }
            }else{
                val = v;
                empty = false;
            }
        };

        T& get() {return val;}

        void onChange(std::function<bool(T,T)> fn){on_change = std::move(fn);};

        void operator= (const T& v){set(v);};
        void operator==(const ValueContainer<T>& v) const {return (v.val == this->val);};
        void operator!=(const ValueContainer<T>& v) const {return (v.val != this->val);};
};

#endif // VALUECONTAINER_H