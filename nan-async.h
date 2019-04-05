#pragma once
#include <nan.h>
#include <uv.h>

namespace NanAsync {

class Worker {
public:
    Worker(Nan::Callback *callback)
    : callback(callback) {
        Nan::HandleScope scope;
        auto obj = Nan::New<v8::Object>();
        persistentHandle.Reset(obj);
        async_resource = new Nan::AsyncResource("NanAsync:Worker", obj);        
    }
    Worker(Nan::ReturnValue<v8::Value>& returnValue)
    : callback(nullptr) {
        Nan::HandleScope scope;
        auto obj = Nan::New<v8::Object>();
        persistentHandle.Reset(obj);
        async_resource = new Nan::AsyncResource("NanAsync:Worker", obj);     
        auto resolver = v8::Promise::Resolver::New(Nan::GetCurrentContext()).ToLocalChecked();
        SaveToPersistent(0, resolver);
        returnValue.Set(resolver->GetPromise());
    }
    virtual ~Worker() {
        if (!persistentHandle.IsEmpty())
            persistentHandle.Reset();        
        delete callback;
        delete async_resource;
    }

    void Start();
    void ExecuteAction();
    void CancelAction();

protected:    
    void SaveToPersistent(uint32_t index, const v8::Local<v8::Value> &value);
    v8::Local<v8::Value> GetFromPersistent(uint32_t index) const;
    virtual void WorkComplete(Nan::Callback* callback) {};
    virtual void WorkComplete(v8::Local<v8::Promise::Resolver>& resolver) {};

    Nan::Callback* callback;
    Nan::AsyncResource *async_resource;
private:
    static void AsyncExecuteComplete(uv_async_t* handle, int status);
    static void CloseCallback(uv_handle_t* handle);

    Nan::Persistent<v8::Object> persistentHandle;
    uv_async_t uv_async{0};
};

inline void Worker::Start() {
    uv_async_init(
        Nan::GetCurrentEventLoop(), 
        &uv_async, 
        reinterpret_cast<uv_async_cb>(AsyncExecuteComplete)
    );
    uv_async.data = this;
}

inline void Worker::ExecuteAction() {
    uv_async_send(&uv_async);
}

inline void Worker::AsyncExecuteComplete(uv_async_t* handle, int status) {
    auto worker = reinterpret_cast<Worker*>(handle->data);
    // This executes on V8 thread
    Nan::HandleScope scope;

    if (worker->callback) {
        worker->WorkComplete(worker->callback);
        delete worker->callback;
        worker->callback = nullptr;
    }
    else {
        auto resolver = worker->GetFromPersistent(0).As<v8::Promise::Resolver>();
        worker->WorkComplete(resolver);
    }

    worker->CancelAction();
}

inline void Worker::CancelAction()
{
    // This is a cancellation of an action registered on V8 thread.
    // Unref the handle to stop preventing the process from exiting.
    uv_unref(reinterpret_cast<uv_handle_t*>(&uv_async));
    uv_close(reinterpret_cast<uv_handle_t*>(&uv_async), CloseCallback);
}

inline void Worker::SaveToPersistent(uint32_t index, const v8::Local<v8::Value> &value) {
    Nan::HandleScope scope;
    Nan::Set(Nan::New(persistentHandle), index, value).FromJust();
}

inline v8::Local<v8::Value> Worker::GetFromPersistent(uint32_t index) const {
    Nan::EscapableHandleScope scope;
    return scope.Escape(Nan::Get(Nan::New(persistentHandle), index).FromMaybe(v8::Local<v8::Value>()));
}

inline void Worker::CloseCallback(uv_handle_t* handle) {
    auto uv_async = reinterpret_cast<uv_async_t*>(handle);
    auto worker = reinterpret_cast<Worker*>(uv_async->data);
    delete worker;
}


}