// Copyright (c) 2014-2015, THUNDERBEAST GAMES LLC All rights reserved
// Please see LICENSE.md in repository root for license information
// https://github.com/AtomicGameEngine/AtomicGameEngine

#include <Atomic/Core/ProcessUtils.h>
#include <Atomic/IO/FileSystem.h>
#include <Atomic/IO/Log.h>
#include <Atomic/Resource/ResourceCache.h>
#include <Atomic/Input/Input.h>
#include <Atomic/Graphics/Renderer.h>
#include <Atomic/Graphics/Graphics.h>
#include <Atomic/Engine/Engine.h>

#ifdef ATOMIC_NETWORK
#include <Atomic/Network/Network.h>
#endif

#include "JSEvents.h"
#include "JSVM.h"
#include "JSComponent.h"
#include "JSCore.h"
#include "JSFileSystem.h"
#include "JSGraphics.h"
#include "JSIO.h"
#include "JSUIAPI.h"
#include "JSScene.h"

#ifdef ATOMIC_NETWORK
#include "JSNetwork.h"
#endif

#include "JSAtomicGame.h"
#include "JSAtomic.h"

#include <Atomic/Scene/Scene.h>
#include <Atomic/Environment/ProcSky.h>

namespace Atomic
{

extern void jsb_package_atomic_init(JSVM* vm);

static int js_module_read_file(duk_context* ctx)
{
    JSVM* vm = JSVM::GetJSVM(ctx);

    ResourceCache* cache = vm->GetContext()->GetSubsystem<ResourceCache>();

    String path = duk_to_string(ctx, 0);

    SharedPtr<File> file = cache->GetFile(path);

    if (!file->IsOpen())
    {
        duk_push_string(ctx, "Unable to open module file");
        duk_throw(ctx);
        return 0;
    }

    unsigned size = file->GetSize();

    SharedArrayPtr<char> data;
    data = new char[size + 1];
    data[size] = '\0';
    file->Read(data, size);

    duk_push_string(ctx, data);

    return 1;
}

static int js_print(duk_context* ctx)
{
    duk_concat(ctx, duk_get_top(ctx));

    VariantMap eventData;
    using namespace JSPrint;
    eventData[P_TEXT] =  duk_to_string(ctx, -1);

    JSVM* vm = JSVM::GetJSVM(ctx);
    vm->SendEvent(E_JSPRINT, eventData);

    LOGINFOF("%s", duk_to_string(ctx, -1));
    return 0;
}

static int js_openConsoleWindow(duk_context* ctx)
{
#ifdef _WIN32
    OpenConsoleWindow();
#endif

    return 0;
}

static int js_assert(duk_context* ctx)
{
    if (!duk_to_boolean(ctx, 0))
    {
        assert(0);
    }
    return 0;
}

static int js_atomic_GetVM(duk_context* ctx)
{
    JSVM* vm = JSVM::GetJSVM(ctx);
    js_push_class_object_instance(ctx, vm);
    return 1;
}


static int js_atomic_GetEngine(duk_context* ctx)
{
    JSVM* vm = JSVM::GetJSVM(ctx);
    js_push_class_object_instance(ctx, vm->GetSubsystem<Engine>());
    return 1;
}

static int js_atomic_GetResourceCache(duk_context* ctx)
{
    JSVM* vm = JSVM::GetJSVM(ctx);
    js_push_class_object_instance(ctx, vm->GetSubsystem<ResourceCache>());
    return 1;
}

static int js_atomic_GetRenderer(duk_context* ctx)
{
    JSVM* vm = JSVM::GetJSVM(ctx);
    js_push_class_object_instance(ctx, vm->GetSubsystem<Renderer>());
    return 1;
}

static int js_atomic_GetGraphics(duk_context* ctx)
{
    JSVM* vm = JSVM::GetJSVM(ctx);
    js_push_class_object_instance(ctx, vm->GetSubsystem<Graphics>());
    return 1;
}

static int js_atomic_GetInput(duk_context* ctx)
{
    JSVM* vm = JSVM::GetJSVM(ctx);
    js_push_class_object_instance(ctx, vm->GetSubsystem<Input>());
    return 1;
}

static int js_atomic_GetFileSystem(duk_context* ctx)
{
    JSVM* vm = JSVM::GetJSVM(ctx);
    js_push_class_object_instance(ctx, vm->GetSubsystem<FileSystem>());
    return 1;
}

#ifdef ATOMIC_NETWORK
static int js_atomic_GetNetwork(duk_context* ctx)
{
    JSVM* vm = JSVM::GetJSVM(ctx);
    js_push_class_object_instance(ctx, vm->GetSubsystem<Network>());
    return 1;
}
#endif

static int js_atomic_script(duk_context* ctx)
{
    JSVM* vm = JSVM::GetJSVM(ctx);

    if (duk_is_string(ctx, 0))
    {
        if ( vm->ExecuteScript(duk_to_string(ctx, 0)))
            duk_push_boolean(ctx, 1);
        else
            duk_push_boolean(ctx, 0);
    }
    else
        duk_push_boolean(ctx, 0);

    return 1;
}

static void js_atomic_destroy_node(Node* node, duk_context* ctx, bool root = false)
{

    if (root)
    {
        PODVector<Node*> children;
        node->GetChildren(children, true);

        for (unsigned i = 0; i < children.Size(); i++)
        {
            if (children.At(i)->JSGetHeapPtr())
                js_atomic_destroy_node(children.At(i), ctx);
        }
    }

    const Vector<SharedPtr<Component> >& components = node->GetComponents();

    for (unsigned i = 0; i < components.Size(); i++)
    {
         Component* component = components[i];

         if (component->GetType() == JSComponent::GetTypeStatic())
         {
             JSComponent* jscomponent = (JSComponent*) component;
             jscomponent->SetDestroyed();
         }

         component->UnsubscribeFromAllEvents();
    }

    node->RemoveAllComponents();
    node->UnsubscribeFromAllEvents();

    if (node->GetParent())
    {
        assert(node->Refs() >= 2);
        node->Remove();
    }

    int top = duk_get_top(ctx);
    duk_push_global_stash(ctx);
    duk_get_prop_index(ctx, -1, JS_GLOBALSTASH_INDEX_NODE_REGISTRY);
    duk_push_pointer(ctx, (void*) node);
    duk_del_prop(ctx, -2);
    duk_pop_2(ctx);
    assert(top = duk_get_top(ctx));
}

static void js_atomic_destroy_scene(Scene* scene, duk_context* ctx)
{
    js_atomic_destroy_node(scene, ctx, true);
}

static int js_atomic_destroy(duk_context* ctx)
{
    if (!duk_is_object(ctx, 0))
        return 0;

    Object* obj = js_to_class_instance<Object>(ctx, 0, 0);

    if (!obj)
        return 0;

    if (obj->GetType() == Node::GetTypeStatic())
    {
        Node* node = (Node*) obj;
        js_atomic_destroy_node(node, ctx, true);
        return 0;
    }
    if (obj->GetType() == Scene::GetTypeStatic())
    {
        Scene* scene = (Scene*) obj;
        js_atomic_destroy_scene(scene, ctx);
        return 0;
    }
    else if (obj->GetType() == JSComponent::GetTypeStatic())
    {
        // FIXME: want to be able to destroy a single component
        assert(0);
        JSComponent* component = (JSComponent*) obj;
        component->UnsubscribeFromAllEvents();
        component->Remove();
        return 0;
    }

    return 0;
}

void jsapi_init_atomic(JSVM* vm)
{
    // core modules
    jsb_package_atomic_init(vm);

    // extensions
    jsapi_init_core(vm);
    jsapi_init_filesystem(vm);
    jsapi_init_io(vm);
#ifdef ATOMIC_NETWORK
    jsapi_init_network(vm);
#endif
    jsapi_init_graphics(vm);
    jsapi_init_ui(vm);
    jsapi_init_scene(vm);

    jsapi_init_atomicgame(vm);

    duk_context* ctx = vm->GetJSContext();

    // globals
    duk_push_global_object(ctx);
    duk_push_c_function(ctx, js_print, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "print");
    duk_push_c_function(ctx, js_assert, 1);
    duk_put_prop_string(ctx, -2, "assert");
    duk_push_c_function(ctx, js_module_read_file, 1);
    duk_put_prop_string(ctx, -2, "js_module_read_file");
    duk_pop(ctx);

    // Atomic
    duk_get_global_string(ctx, "Atomic");

    String platform = GetPlatform();
    if (platform == "Mac OS X")
        platform = "MacOSX";

    duk_push_string(ctx, platform.CString());
    duk_put_prop_string(ctx, -2, "platform");

    // Node registry
    duk_push_global_stash(ctx);
    duk_push_object(ctx);
    duk_put_prop_index(ctx, -2, JS_GLOBALSTASH_INDEX_NODE_REGISTRY);
    duk_pop(ctx);

    duk_push_c_function(ctx, js_openConsoleWindow, 0);
    duk_put_prop_string(ctx, -2, "openConsoleWindow");

    duk_push_c_function(ctx, js_atomic_GetVM, 0);
    duk_put_prop_string(ctx, -2, "getVM");

    duk_push_c_function(ctx, js_atomic_GetEngine, 0);
    duk_put_prop_string(ctx, -2, "getEngine");

    duk_push_c_function(ctx, js_atomic_GetGraphics, 0);
    duk_put_prop_string(ctx, -2, "getGraphics");

    duk_push_c_function(ctx, js_atomic_GetRenderer, 0);
    duk_put_prop_string(ctx, -2, "getRenderer");

    duk_push_c_function(ctx, js_atomic_GetResourceCache, 0);
    duk_put_prop_string(ctx, -2, "getResourceCache");

    duk_push_c_function(ctx, js_atomic_GetInput, 0);
    duk_put_prop_string(ctx, -2, "getInput");

    duk_push_c_function(ctx, js_atomic_GetFileSystem, 0);
    duk_put_prop_string(ctx, -2, "getFileSystem");

#ifdef ATOMIC_NETWORK
    duk_push_c_function(ctx, js_atomic_GetNetwork, 0);
    duk_put_prop_string(ctx, -2, "getNetwork");
#endif

    duk_push_c_function(ctx, js_atomic_script, 1);
    duk_put_prop_string(ctx, -2, "script");

    duk_push_c_function(ctx, js_atomic_destroy, 1);
    duk_put_prop_string(ctx, -2, "destroy");


    duk_pop(ctx);


}

}