#include <PLAwesomium/MethodDispatcher.h>
#include <algorithm>
#include <iostream>
#include "Awesomium/STLHelpers.h"
// -------------------------------------------------------------------------
MethodDispatcher::MethodDispatcher()
{

}

// -------------------------------------------------------------------------
void MethodDispatcher::Bind(Awesomium::JSObject& object, const Awesomium::WebString& name, JSDelegate callback)
{
	// We can't bind methods to local JSObjects
	if (object.type() == Awesomium::kJSObjectType_Local)
		return;
		
	object.SetCustomMethod(name, false);

	ObjectMethodKey key(object.remote_id(), name);
	bound_methods_[key] = callback;
}

// -------------------------------------------------------------------------
void MethodDispatcher::BindWithRetval(Awesomium::JSObject& object, const Awesomium::WebString& name, JSDelegateWithRetval callback)
{
	std::cout << "?? " << Awesomium::ToString(name)<< std::endl;

	// We can't bind methods to local JSObjects
	if (object.type() == Awesomium::kJSObjectType_Local)
		return;

	object.SetCustomMethod(name, true);

	ObjectMethodKey key(object.remote_id(), name);
	bound_methods_with_retval_[key] = callback;
}

// -------------------------------------------------------------------------
void MethodDispatcher::OnMethodCall(Awesomium::WebView* caller, unsigned int remote_object_id, const Awesomium::WebString& method_name, const Awesomium::JSArray& args)
{
	// Find the method that matches the object id + method name
	std::map<ObjectMethodKey, JSDelegate>::iterator i = bound_methods_.find(ObjectMethodKey(remote_object_id, method_name));

	// Call the method
	if (i != bound_methods_.end())
		i->second(caller, args);
}

// -------------------------------------------------------------------------
void MethodDispatcher::Clear()
{
	bound_methods_.clear();
	bound_methods_with_retval_.clear();
}

// -------------------------------------------------------------------------
Awesomium::JSValue MethodDispatcher::OnMethodCallWithReturnValue(Awesomium::WebView* caller,
																 unsigned int remote_object_id,
																 const Awesomium::WebString& method_name,
																 const Awesomium::JSArray& args) {
	// Find the method that matches the object id + method name
	std::map<ObjectMethodKey, JSDelegateWithRetval>::iterator i =
		bound_methods_with_retval_.find(ObjectMethodKey(remote_object_id, method_name));

	// Call the method
	if (i != bound_methods_with_retval_.end())
		return i->second(caller, args);

	return Awesomium::JSValue::Undefined();
}

// -------------------------------------------------------------------------
void MethodDispatcher::Unbind(const Awesomium::WebString& name, bool hasRetVal)
{
	if(hasRetVal)
	{
		auto it = std::find_if(bound_methods_with_retval_.begin(), bound_methods_with_retval_.end(),
		[&name](const std::pair<const ObjectMethodKey, JSDelegateWithRetval>& data)
		{
			return data.first.second == name;
		});

		if(it != bound_methods_with_retval_.end())
		{
			bound_methods_with_retval_.erase(it);
		}
		return;
	}

	auto it = std::find_if(bound_methods_.begin(), bound_methods_.end(),
		[&name](const std::pair<const ObjectMethodKey, JSDelegate>& data)
	{
		return data.first.second == name;
	});

	if(it != bound_methods_.end())
	{
		bound_methods_.erase(it);
	}
}

// -------------------------------------------------------------------------
size_t MethodDispatcher::CallbackCount()
{
	return bound_methods_.size() + bound_methods_with_retval_.size();
}

// -------------------------------------------------------------------------
bool MethodDispatcher::HasMethod(const Awesomium::WebString& name)
{
	auto it = std::find_if(bound_methods_.begin(), bound_methods_.end(),
		[&name](const std::pair<const ObjectMethodKey, JSDelegate>& data)
	{
		return data.first.second == name;
	});

	if(it != bound_methods_.end())
		return true;

	auto itRetVal = std::find_if(bound_methods_with_retval_.begin(), bound_methods_with_retval_.end(),
		[&name](const std::pair<const ObjectMethodKey, JSDelegateWithRetval>& data)
	{
		return data.first.second == name;
	});

	if(itRetVal != bound_methods_with_retval_.end())
		return true;

	return false;
}
