#include <rct/String.h>
#include <rct/Path.h>
#include <rct/Log.h>
#include <v8.h>

v8::Handle<v8::String> toJson(v8::Handle<v8::Value> obj)
{
    v8::HandleScope scope;

    v8::Handle<v8::Context> context = v8::Context::GetCurrent();
    v8::Handle<v8::Object> global = context->Global();

    v8::Handle<v8::Object> JSON = global->Get(v8::String::New("JSON"))->ToObject();
    v8::Handle<v8::Function> JSON_stringify = v8::Handle<v8::Function>::Cast(JSON->Get(v8::String::New("stringify")));

    v8::Handle<v8::Value> args[3];
    args[0] = obj;
    args[1] = v8::Null();
    args[2] = v8::String::New("    ");

    return scope.Close(JSON_stringify->Call(JSON, 3, args)->ToString());
}

struct Cursor
{
    String target;
    Set<int> references;
};

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("[%s] %s:%d: if (argc < 2) {} [after]\n", __func__, __FILE__, __LINE__);
        return 1;
    }
    v8::HandleScope handleScope;
    v8::Handle<v8::Context> ctx = v8::Context::New();
    v8::Context::Scope scope(ctx);
    String c;
    Path file(argv[1]);
    file.resolve();
    if (argc > 2) {
        c = argv[2];
    } else {
        c = file.readAll();
    }

    // error() << c;

    const String esprimaSrcString = Path(ESPRIMA_JS).readAll();
    v8::Handle<v8::String> esprimaSrc = v8::String::New(esprimaSrcString.constData(), esprimaSrcString.size());

    // v8::Handle<v8::String> code = v8::String::New(c.constData());
    v8::TryCatch tryCatch;
    v8::Handle<v8::Script> script = v8::Script::Compile(esprimaSrc);
    if (tryCatch.HasCaught() || script.IsEmpty() || !tryCatch.Message().IsEmpty()) {
        v8::Handle<v8::Message> message = tryCatch.Message();
        v8::String::Utf8Value msg(message->Get());
        printf("%s:%d:%d: esprima error: %s {%d-%d}\n", file.constData(), message->GetLineNumber(),
               message->GetStartColumn(), *msg, message->GetStartPosition(), message->GetEndPosition());
        return 1;
    }
    v8::Handle<v8::Value> result = script->Run();
    v8::String::Utf8Value var(result);
    printf("Result [%s]\n", *var);

    v8::Handle<v8::Object> global = ctx->Global();
    assert(!global.IsEmpty());
    v8::Handle<v8::Object> esprima = global->Get(v8::String::New("esprima"))->ToObject();
    if (!esprima->IsObject()) {
        printf("[%s] %s:%d: if (!value->IsObject()) { [after]\n", __func__, __FILE__, __LINE__);
        return 1;
    }
    // Handle<Value> args[2];
    // args[0] = v8::String::New("value1");
    // args[1] = v8::String::New("value2");

    v8::Handle<v8::Function> parse = v8::Handle<v8::Function>::Cast(esprima->Get(v8::String::New("parse")));
    v8::Handle<v8::Value> args[2];
    // args[0] = v8::String::New(file.constData(), file.size());
    args[0] = v8::String::New(c.constData(), c.size());
    v8::Handle<v8::Object> options = v8::Object::New();
    // options->Set(v8::String::New("loc"), v8::Boolean::New(true));
    options->Set(v8::String::New("range"), v8::Boolean::New(true));
    options->Set(v8::String::New("tolerant"), v8::Boolean::New(true));
    args[1] = options;
    result = parse->Call(esprima, 2, args);

    printf("Parse: %s\n", *v8::String::Utf8Value(toJson(result)));
    return 0;
}
