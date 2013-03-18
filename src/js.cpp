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
    Cursor()
        : length(0), parent(-1), type(Invalid) {}
    int length, parent;
    Set<int> references;
    String name;
    enum Type {
        Invalid,
        Declaration,
        Reference
    } type;
};

inline Log operator<<(Log log, const Cursor &cursor)
{
    log << String::format<128>("name: %s length: %d type: %s",
                               cursor.name.constData(),
                               cursor.length,
                               cursor.type == Cursor::Declaration ? "declaration" :
                               (cursor.type == Cursor::Reference ? "reference" : "invalid"));
    return log;
}

class Visitor
{
public:
    bool visit(v8::Handle<v8::Object> object);

    const Map<int, Cursor> &cursors() const { return mCursors; }
private:
    bool visitRecursive(v8::Handle<v8::Object> object);
    bool visitDeclarations(v8::Handle<v8::Array> object);

    Map<int, Cursor> mCursors;
};

template <typename T>
static v8::Handle<T> get(v8::Handle<v8::Object> object, const char *property)
{
    v8::HandleScope scope;
    v8::Handle<v8::Value> prop = object->Get(v8::String::New(property));
    return scope.Close(v8::Handle<T>::Cast(prop));
}

template <typename T>
static v8::Handle<T> get(v8::Handle<v8::Array> object, int index)
{
    v8::HandleScope scope;
    v8::Handle<v8::Value> prop = object->Get(index);
    return scope.Close(v8::Handle<T>::Cast(prop));
}

static inline bool operator==(const char *l, v8::Handle<v8::String> r)
{
    return r.IsEmpty() ? -1 : !strcmp(l, *v8::String::Utf8Value(r));
}

static inline bool operator==(v8::Handle<v8::String> l, const char *r)
{
    return l.IsEmpty() ? 1 : !strcmp(*v8::String::Utf8Value(l), r);
}

#define toCString(str) *v8::String::Utf8Value(str)

bool Visitor::visit(v8::Handle<v8::Object> object)
{
    v8::HandleScope handleScope;
    assert(!object.IsEmpty());
    // v8::Handle<v8::Value> b = parse->Get(v8::String::New("body"));
    // printf("isnull %d\n", b.IsEmpty());
    // printf("foo[%s]\n", toCString(toJson(b)));

    v8::Handle<v8::Array> body = get<v8::Array>(object, "body");
    if (body.IsEmpty()) {
        return false;
    }

    for (unsigned i=0; i<body->Length(); ++i) {
        if (!visitRecursive(get<v8::Object>(body, i))) {
            error("Invalid body element at index %d", i);
        }
    }
    return true;
}

bool Visitor::visitRecursive(v8::Handle<v8::Object> object)
{
    v8::HandleScope handleScope;
    if (object.IsEmpty())
        return false;

    v8::Handle<v8::String> type = get<v8::String>(object, "type");
    assert(!type.IsEmpty());
    if (type == "VariableDeclaration") {
        visitDeclarations(get<v8::Array>(object, "declarations"));
    } else {
        printf("%s\n", toCString(type));
        // printf("[%s] %s:%d: } else { [after]\n", __func__, __FILE__, __LINE__);
    }
    return true;
}

bool Visitor::visitDeclarations(v8::Handle<v8::Array> declarations)
{
    if (declarations.IsEmpty() || !declarations->IsArray()) {
        return false;
    }
    for (unsigned i=0; i<declarations->Length(); ++i) {
        v8::Handle<v8::Object> declaration = get<v8::Object>(declarations, i);
        assert(!declaration.IsEmpty());
        v8::Handle<v8::String> type = get<v8::String>(declaration, "type");
        if (type == "VariableDeclarator") {
            v8::Handle<v8::Object> id = get<v8::Object>(declaration, "id");
            assert(!id.IsEmpty() && id->IsObject());
            v8::Handle<v8::String> name = get<v8::String>(id, "name");
            v8::Handle<v8::Array> range = get<v8::Array>(id, "range");
            assert(!range.IsEmpty() && range->Length() == 2);
            const int offset = get<v8::Integer>(range, 0)->Value();
            const int length = get<v8::Integer>(range, 1)->Value() - offset;
            Cursor c;
            c.name = String(toCString(name), name->Length());
            c.type = Cursor::Declaration;
            c.length = length;
            mCursors[offset] = c;
        }
    }
    return true;
}


int main(int argc, char **argv)
{
    Map<int, Cursor> cursors;
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
    // printf("Result [%s]\n", *var);

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

    if (!getenv("NO_JSON"))
        printf("Parse: %s\n", *v8::String::Utf8Value(toJson(result)));

    Visitor visitor;
    visitor.visit(result->ToObject());
    error() << visitor.cursors();
    return 0;
}
