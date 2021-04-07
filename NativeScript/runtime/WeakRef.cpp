#include "WeakRef.h"
#include "NativeScriptException.h"
#include "ArgConverter.h"
#include "Caches.h"
#include "Helpers.h"

using namespace v8;

namespace tns {

void WeakRef::Init(Local<Context> context) {
    Isolate* isolate = context->GetIsolate();

    Local<v8::Function> weakRefCtorFunc;
    bool success = Function::New(context, ConstructorCallback, Local<Value>()).ToLocal(&weakRefCtorFunc);
    tns::Assert(success, isolate);

    Local<v8::String> name = tns::ToV8String(isolate, "WeakRef");
    Local<Object> global = context->Global();
    success = global->Set(context, name, weakRefCtorFunc).FromMaybe(false);
    tns::Assert(success, isolate);
}

void WeakRef::ConstructorCallback(const FunctionCallbackInfo<Value>& info) {
    Isolate* isolate = info.GetIsolate();
    Isolate::Scope isolate_scope(isolate);
    HandleScope handle_scope(isolate);
    
    tns::Assert(info.IsConstructCall(), isolate);
    try {
        if (info.Length() < 1 || !info[0]->IsObject()) {
            throw NativeScriptException("Argument must be an object.");
        }

        Local<Object> target = info[0].As<Object>();
        Local<Context> context = isolate->GetCurrentContext();

        // std::shared_ptr< Persistent<Value> > poValue = ArgConverter::CreateEmptyObject(context);
        // Local<Object> weakRef = poValue->Get(isolate).As<Object>();
        // poValue->Reset();
        Local<Object> weakRef = v8::Object::New(isolate);

        Persistent<Object>* poTarget = new Persistent<Object>(isolate, target);
        Persistent<Object>* poHolder = new Persistent<Object>(isolate, weakRef);
        CallbackState* callbackState = new CallbackState(poTarget, poHolder);

        poTarget->SetWeak(callbackState, WeakTargetCallback, WeakCallbackType::kFinalizer);
        poHolder->SetWeak(callbackState, WeakHolderCallback, WeakCallbackType::kFinalizer);

        bool success = weakRef->Set(context, tns::ToV8String(isolate, "get"), GetGetterFunction(isolate)).FromMaybe(false);
        tns::Assert(success, isolate);

        success = weakRef->Set(context, tns::ToV8String(isolate, "deref"), GetGetterFunction(isolate)).FromMaybe(false);
        tns::Assert(success, isolate);

        success = weakRef->Set(context, tns::ToV8String(isolate, "clear"), GetClearFunction(isolate)).FromMaybe(false);
        tns::Assert(success, isolate);

        tns::SetPrivateValue(weakRef, tns::ToV8String(isolate, "target"), External::New(isolate, poTarget));

        info.GetReturnValue().Set(weakRef);
    } catch (NativeScriptException& ex) {
        ex.ReThrowToV8(isolate);
    }
}

void WeakRef::WeakTargetCallback(const WeakCallbackInfo<CallbackState>& data) {\
    CallbackState* callbackState = data.GetParameter();
    Persistent<Object>* poTarget = callbackState->target_;
    poTarget->Reset();
    delete poTarget;
    callbackState->target_ = nullptr;

    Isolate* isolate = data.GetIsolate();
    Persistent<Object>* poHolder = callbackState->holder_;
    if (poHolder != nullptr) {
        Local<Object> holder = poHolder->Get(isolate);
        tns::SetPrivateValue(holder, tns::ToV8String(isolate, "target"), External::New(isolate, nullptr));
    }

    if (callbackState->holder_ == nullptr) {
        delete callbackState;
    }
}

void WeakRef::WeakHolderCallback(const WeakCallbackInfo<CallbackState>& data) {
    CallbackState* callbackState = data.GetParameter();
    Persistent<Object>* poHolder = callbackState->holder_;
    Isolate* isolate = data.GetIsolate();
    Local<Object> holder = Local<Object>::New(isolate, *poHolder);

    Local<Value> hiddenVal = tns::GetPrivateValue(holder, tns::ToV8String(isolate, "target"));
    Persistent<Object>* poTarget = reinterpret_cast<Persistent<Object>*>(hiddenVal.As<External>()->Value());

    if (poTarget != nullptr) {
        poHolder->SetWeak(callbackState, WeakHolderCallback, WeakCallbackType::kFinalizer);
    } else {
        poHolder->Reset();
        delete poHolder;
        callbackState->holder_ = nullptr;
        if (callbackState->target_ == nullptr) {
            delete callbackState;
        }
    }
}

Local<v8::Function> WeakRef::GetGetterFunction(Isolate* isolate) {
    auto cache = Caches::Get(isolate);
    Persistent<v8::Function>* poGetter = cache->WeakRefGetterFunc.get();
    if (poGetter != nullptr) {
        return poGetter->Get(isolate);
    }

    Local<Context> context = isolate->GetCurrentContext();
    Local<v8::Function> getterFunc;
    bool success = FunctionTemplate::New(isolate, GetCallback)->GetFunction(context).ToLocal(&getterFunc);
    tns::Assert(success, isolate);
    cache->WeakRefGetterFunc = std::make_unique<Persistent<v8::Function>>(isolate, getterFunc);
    return getterFunc;
}

Local<v8::Function> WeakRef::GetClearFunction(Isolate* isolate) {
    auto cache = Caches::Get(isolate);
    Persistent<v8::Function>* poClear = cache->WeakRefClearFunc.get();
    if (poClear != nullptr) {
        return poClear->Get(isolate);
    }

    Local<Context> context = isolate->GetCurrentContext();
    Local<v8::Function> clearFunc = FunctionTemplate::New(isolate, ClearCallback)->GetFunction(context).ToLocalChecked();
    cache->WeakRefClearFunc = std::make_unique<Persistent<v8::Function>>(isolate, clearFunc);
    return clearFunc;
}

void WeakRef::GetCallback(const FunctionCallbackInfo<Value>& info) {
    Local<Object> holder = info.This();
    Isolate* isolate = info.GetIsolate();
    Local<Value> hiddenVal = tns::GetPrivateValue(holder, tns::ToV8String(isolate, "target"));
    Persistent<Object>* poTarget = reinterpret_cast<Persistent<Object>*>(hiddenVal.As<External>()->Value());

    if (poTarget != nullptr) {
        Local<Object> target = poTarget->Get(isolate);
        info.GetReturnValue().Set(target);
    } else {
        info.GetReturnValue().Set(Null(isolate));
    }
}

void WeakRef::ClearCallback(const FunctionCallbackInfo<Value>& info) {
    Local<Object> holder = info.This();
    Isolate* isolate = info.GetIsolate();
    tns::SetPrivateValue(holder, tns::ToV8String(isolate, "target"), External::New(isolate, nullptr));
}

}
