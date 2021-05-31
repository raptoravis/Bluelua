#include "LuaObjectBase.h"

#include "Runtime/Launch/Resources/Version.h"
#include "UObject/Class.h"
#include "UObject/EnumProperty.h"
#include "UObject/TextProperty.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

#include "Bluelua.h"
#include "Delegates/LuaMulticastScriptDelegate.h"
#include "Delegates/LuaScriptDelegate.h"
#include "Delegates/LuaSparseDelegate.h"
#include "lua.hpp"
#include "LuaImplementableInterface.h"
#include "LuaUClass.h"
#include "LuaUDelegate.h"
#include "LuaUObject.h"
#include "LuaUStruct.h"

DECLARE_CYCLE_STAT(TEXT("PushPropertyToLua"), STAT_PushPropertyToLua, STATGROUP_Bluelua);
DECLARE_CYCLE_STAT(TEXT("FetchPropertyFromLua"), STAT_FetchPropertyFromLua, STATGROUP_Bluelua);

static TMap<FFieldClass*, FLuaObjectBase::PushPropertyFunction> GPusherMap;
static TMap<FFieldClass*, FLuaObjectBase::FetchPropertyFunction> GFetcherMap;

template<typename T>
static int PushBaseProperty(lua_State* L, UProperty* Property, void* Params, UObject* Object, bool)
{
	auto CastedProperty = Cast<T>(Property);
	if (!CastedProperty)
	{
		lua_pushnil(L);
		return 1;
	}

	return FLuaObjectBase::Push(L, CastedProperty->GetPropertyValue(Params));
}

template<typename T>
static bool FetchBaseProperty(lua_State* L, UProperty* Property, void* Params, int32 Index)
{
	auto CastedProperty = Cast<T>(Property);
	if (!CastedProperty)
	{
		return false;
	}

	typename T::TCppType Value;
	if (!FLuaObjectBase::Fetch(L, Index, Value))
	{
		return false;
	}

	CastedProperty->SetPropertyValue(Params, Value);

	return true;
}

template<typename T>
static void RegisterPusher(FLuaObjectBase::PushPropertyFunction PushFunction)
{
	GPusherMap.Add(T::StaticClass(), PushFunction);
}

template<typename T>
static void RegisterFetcher(FLuaObjectBase::FetchPropertyFunction FetchFunction)
{
	GFetcherMap.Add(T::StaticClass(), FetchFunction);
}

void FLuaObjectBase::Init()
{
	RegisterPusher<UByteProperty>(PushBaseProperty<UByteProperty>);
	RegisterPusher<UInt8Property>(PushBaseProperty<UInt8Property>);
	RegisterPusher<UInt16Property>(PushBaseProperty<UInt16Property>);
	RegisterPusher<UUInt16Property>(PushBaseProperty<UUInt16Property>);
	RegisterPusher<UIntProperty>(PushBaseProperty<UIntProperty>);
	RegisterPusher<UUInt32Property>(PushBaseProperty<UUInt32Property>);
	RegisterPusher<UInt64Property>(PushBaseProperty<UInt64Property>);
	RegisterPusher<UUInt64Property>(PushBaseProperty<UUInt64Property>);
	RegisterPusher<UBoolProperty>(PushBaseProperty<UBoolProperty>);
	RegisterPusher<UFloatProperty>(PushBaseProperty<UFloatProperty>);
	RegisterPusher<UDoubleProperty>(PushBaseProperty<UDoubleProperty>);
	RegisterPusher<UStrProperty>(PushBaseProperty<UStrProperty>);
	RegisterPusher<UTextProperty>(PushBaseProperty<UTextProperty>);
	RegisterPusher<UNameProperty>(PushBaseProperty<UNameProperty>);
	RegisterPusher<UStructProperty>(PushStructProperty);
	RegisterPusher<UEnumProperty>(PushEnumProperty);
	RegisterPusher<UClassProperty>(PushClassProperty);
	RegisterPusher<UObjectProperty>(PushObjectProperty);
	RegisterPusher<UArrayProperty>(PushArrayProperty);
	RegisterPusher<USetProperty>(PushSetProperty);
	RegisterPusher<UMapProperty>(PushMapProperty);
#if ENGINE_MINOR_VERSION >= 23
	RegisterPusher<UMulticastInlineDelegateProperty>(PushMulticastInlineDelegateProperty);
	RegisterPusher<UMulticastSparseDelegateProperty>(PushMulticastSparseDelegateProperty);
#else
	RegisterPusher<UMulticastDelegateProperty>(PushMulticastDelegateProperty);
#endif // ENGINE_MINOR_VERSION >= 23
	RegisterPusher<UDelegateProperty>(PushDelegateProperty);

	RegisterFetcher<UByteProperty>(FetchBaseProperty<UByteProperty>);
	RegisterFetcher<UInt8Property>(FetchBaseProperty<UInt8Property>);
	RegisterFetcher<UInt16Property>(FetchBaseProperty<UInt16Property>);
	RegisterFetcher<UUInt16Property>(FetchBaseProperty<UUInt16Property>);
	RegisterFetcher<UIntProperty>(FetchBaseProperty<UIntProperty>);
	RegisterFetcher<UUInt32Property>(FetchBaseProperty<UUInt32Property>);
	RegisterFetcher<UInt64Property>(FetchBaseProperty<UInt64Property>);
	RegisterFetcher<UUInt64Property>(FetchBaseProperty<UUInt64Property>);
	RegisterFetcher<UBoolProperty>(FetchBaseProperty<UBoolProperty>);
	RegisterFetcher<UFloatProperty>(FetchBaseProperty<UFloatProperty>);
	RegisterFetcher<UDoubleProperty>(FetchBaseProperty<UDoubleProperty>);
	RegisterFetcher<UStrProperty>(FetchBaseProperty<UStrProperty>);
	RegisterFetcher<UTextProperty>(FetchBaseProperty<UTextProperty>);
	RegisterFetcher<UNameProperty>(FetchBaseProperty<UNameProperty>);
	RegisterFetcher<UStructProperty>(FetchStructProperty);
	RegisterFetcher<UEnumProperty>(FetchEnumProperty);
	RegisterFetcher<UClassProperty>(FetchClassProperty);
	RegisterFetcher<UObjectProperty>(FetchObjectProperty);
	RegisterFetcher<UArrayProperty>(FetchArrayProperty);
	RegisterFetcher<USetProperty>(FetchSetProperty);
	RegisterFetcher<UMapProperty>(FetchMapProperty);
#if ENGINE_MINOR_VERSION >= 23
	RegisterFetcher<UMulticastInlineDelegateProperty>(FetchMulticastInlineDelegateProperty);
	RegisterFetcher<UMulticastSparseDelegateProperty>(FetchMulticastSparseDelegateProperty);
#else
	RegisterFetcher<UMulticastDelegateProperty>(FetchMulticastDelegateProperty);
#endif // ENGINE_MINOR_VERSION >= 23
	RegisterFetcher<UDelegateProperty>(FetchDelegateProperty);
}

FLuaObjectBase::PushPropertyFunction FLuaObjectBase::GetPusher(FFieldClass* Class)
{
	auto PusherIter = GPusherMap.Find(Class);

	return PusherIter ? *PusherIter : nullptr;
}

FLuaObjectBase::FetchPropertyFunction FLuaObjectBase::GetFetcher(FFieldClass* Class)
{
	auto FetcherIter = GFetcherMap.Find(Class);

	return FetcherIter ? *FetcherIter : nullptr;
}

int FLuaObjectBase::PushProperty(lua_State* L, UProperty* Property, void* Params, UObject* Object/* = nullptr*/, bool bCopyValue/* = true*/)
{
	SCOPE_CYCLE_COUNTER(STAT_PushPropertyToLua);

	FFieldClass* PropertyClass = Property->GetClass();
	auto Pusher = GetPusher(PropertyClass);
	if (Pusher)
	{
		return Pusher(L, Property, Params, Object, bCopyValue);
	}
	else
	{
		lua_pushnil(L);

		UE_LOG(LogBluelua, Error, TEXT("Push property[%s] failed! Unkown type[%s]!"), *Property->GetName(), PropertyClass ? *PropertyClass->GetName() : TEXT(""));

		return 1;
	}
}

int FLuaObjectBase::PushStructProperty(lua_State* L, UProperty* Property, void* Params, UObject* Object, bool bCopyValue/* = true*/)
{
	UStructProperty* StructProperty = Cast<UStructProperty>(Property);

	if (UScriptStruct* ScriptStruct = Cast<UScriptStruct>(StructProperty->Struct))
	{
		FLuaUStruct::Push(L, ScriptStruct, Params, bCopyValue);
	}
	else
	{
		lua_pushnil(L);
	}

	return 1;
}

int FLuaObjectBase::PushEnumProperty(lua_State* L, UProperty* Property, void* Params, UObject* Object, bool)
{
	UEnumProperty* EnumProperty = Cast<UEnumProperty>(Property);

	lua_pushinteger(L, EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(Params));

	return 1;
}

int FLuaObjectBase::PushClassProperty(lua_State* L, UProperty* Property, void* Params, UObject* Object, bool)
{
	UClassProperty* ClassProperty = Cast<UClassProperty>(Property);

	return FLuaUClass::Push(L, Cast<UClass>(ClassProperty->GetObjectPropertyValue(Params)));
}

int FLuaObjectBase::PushObjectProperty(lua_State* L, UProperty* Property, void* Params, UObject* Object, bool)
{
	UObjectProperty* ObjectProperty = Cast<UObjectProperty>(Property);

	return FLuaUObject::Push(L, ObjectProperty->GetObjectPropertyValue(Params));
}

int FLuaObjectBase::PushArrayProperty(lua_State* L, UProperty* Property, void* Params, UObject* Object, bool)
{
	UArrayProperty* ArrayProperty = Cast<UArrayProperty>(Property);

	FScriptArrayHelper ArrayHelper(ArrayProperty, Params);
	const int32 Num = ArrayHelper.Num();

	lua_newtable(L);
	for (int32 Index = 0; Index < Num; ++Index)
	{
		PushProperty(L, ArrayProperty->Inner, ArrayHelper.GetRawPtr(Index));
		lua_seti(L, -2, Index + 1);
	}

	return 1;
}

int FLuaObjectBase::PushSetProperty(lua_State* L, UProperty* Property, void* Params, UObject* Object, bool)
{
	USetProperty* SetProperty = Cast<USetProperty>(Property);

	FScriptSetHelper SetHelper(SetProperty, Params);
	const int32 Num = SetHelper.Num();

	lua_newtable(L);
	for (int Index = 0; Index < Num; ++Index)
	{
		PushProperty(L, SetProperty->ElementProp, SetHelper.GetElementPtr(Index));
		lua_seti(L, -2, Index + 1);
	}

	return 1;
}

int FLuaObjectBase::PushMapProperty(lua_State* L, UProperty* Property, void* Params, UObject* Object, bool)
{
	UMapProperty* MapProperty = Cast<UMapProperty>(Property);

	FScriptMapHelper MapHelper(MapProperty, Params);
	const int32 Num = MapHelper.Num();

	lua_newtable(L);
	for (int Index = 0; Index < Num; ++Index)
	{
		uint8* PairPtr = MapHelper.GetPairPtr(Index);
		PushProperty(L, MapProperty->KeyProp, PairPtr/* + MapProperty->MapLayout.KeyOffset*/);
		PushProperty(L, MapProperty->ValueProp, PairPtr + MapProperty->MapLayout.ValueOffset);
		lua_settable(L, -3);
	}

	return 1;
}

int FLuaObjectBase::PushMulticastDelegateProperty(lua_State* L, UProperty* Property, void* Params, UObject* Object, bool)
{
#if ENGINE_MINOR_VERSION >= 23
	return 0;
#else
	UMulticastDelegateProperty* DelegateProperty = Cast<UMulticastDelegateProperty>(Property);

	return FLuaUDelegate::Push(L, DelegateProperty->GetPropertyValuePtr(Params), DelegateProperty->SignatureFunction, FLuaMulticastScriptDelegate::Create);
#endif // ENGINE_MINOR_VERSION >= 23
}

int FLuaObjectBase::PushMulticastInlineDelegateProperty(lua_State* L, UProperty* Property, void* Params, UObject* Object, bool)
{
#if ENGINE_MINOR_VERSION >= 23
	UMulticastInlineDelegateProperty* DelegateProperty = Cast<UMulticastInlineDelegateProperty>(Property);

	return FLuaUDelegate::Push(L, DelegateProperty->GetPropertyValuePtr(Params), DelegateProperty->SignatureFunction, FLuaMulticastScriptDelegate::Create);
#else
	return 0;
#endif // ENGINE_MINOR_VERSION >= 23
}

int FLuaObjectBase::PushMulticastSparseDelegateProperty(lua_State* L, UProperty* Property, void* Params, UObject* Object, bool)
{
#if ENGINE_MINOR_VERSION >= 23
	UMulticastSparseDelegateProperty* DelegateProperty = Cast<UMulticastSparseDelegateProperty>(Property);

	return FLuaUDelegate::Push(L, DelegateProperty->GetPropertyValuePtr(Params), DelegateProperty->SignatureFunction, FLuaSparseDelegate::Create);
#else
	return 0;
#endif // ENGINE_MINOR_VERSION >= 23
}

int FLuaObjectBase::PushDelegateProperty(lua_State* L, UProperty* Property, void* Params, UObject* Object, bool)
{
	auto DelegateProperty = Cast<UDelegateProperty>(Property);

	return FLuaUDelegate::Push(L, DelegateProperty->GetPropertyValuePtr(Params), DelegateProperty->SignatureFunction, FLuaScriptDelegate::Create);
}

int FLuaObjectBase::Push(lua_State* L, int8 Value)
{
	lua_pushinteger(L, Value);
	return 1;
}

int FLuaObjectBase::Push(lua_State* L, uint8 Value)
{
	lua_pushinteger(L, Value);
	return 1;
}

int FLuaObjectBase::Push(lua_State* L, int16 Value)
{
	lua_pushinteger(L, Value);
	return 1;
}

int FLuaObjectBase::Push(lua_State* L, uint16 Value)
{
	lua_pushinteger(L, Value);
	return 1;
}

int FLuaObjectBase::Push(lua_State* L, int32 Value)
{
	lua_pushinteger(L, Value);
	return 1;
}

int FLuaObjectBase::Push(lua_State* L, uint32 Value)
{
	lua_pushinteger(L, Value);
	return 1;
}

int FLuaObjectBase::Push(lua_State* L, int64 Value)
{
	lua_pushinteger(L, Value);
	return 1;
}

int FLuaObjectBase::Push(lua_State* L, uint64 Value)
{
	lua_pushinteger(L, Value);
	return 1;
}

int FLuaObjectBase::Push(lua_State* L, float Value)
{
	lua_pushnumber(L, Value);
	return 1;
}

int FLuaObjectBase::Push(lua_State* L, double Value)
{
	lua_pushnumber(L, Value);
	return 1;
}

int FLuaObjectBase::Push(lua_State* L, bool Value)
{
	lua_pushboolean(L, Value);
	return 1;
}

int FLuaObjectBase::Push(lua_State* L, const FString& Value)
{
	lua_pushstring(L, TCHAR_TO_UTF8(*Value));
	return 1;
}

int FLuaObjectBase::Push(lua_State* L, const FText& Value)
{
	lua_pushstring(L, TCHAR_TO_UTF8(*Value.ToString()));
	return 1;
}

int FLuaObjectBase::Push(lua_State* L, const FName& Value)
{
	lua_pushstring(L, TCHAR_TO_UTF8(*Value.ToString()));
	return 1;
}

bool FLuaObjectBase::FetchProperty(lua_State* L, UProperty* Property, void* Params, int32 Index)
{
	SCOPE_CYCLE_COUNTER(STAT_FetchPropertyFromLua);

	auto Fetcher = GetFetcher(Property->GetClass());
	if (Fetcher)
	{
		return Fetcher(L, Property, Params, Index);
	}
	else
	{
		UE_LOG(LogBluelua, Error, TEXT("Fetch property[%s] failed! Unkown type!"), *Property->GetName());

		return false;
	}
}

bool FLuaObjectBase::FetchStructProperty(lua_State* L, UProperty* Property, void* Params, int32 Index)
{
	if (auto StructProperty = Cast<UStructProperty>(Property))
	{
		return FLuaUStruct::Fetch(L, Index, StructProperty->Struct, (uint8*)Params);
	}

	return false;
}

bool FLuaObjectBase::FetchEnumProperty(lua_State* L, UProperty* Property, void* Params, int32 Index)
{
	if (auto EnumProperty = Cast<UEnumProperty>(Property))
	{
		UNumericProperty* UnderlyingProperty = EnumProperty->GetUnderlyingProperty();
		UnderlyingProperty->SetIntPropertyValue(Params, lua_tointeger(L, Index));
	}

	return false;
}

bool FLuaObjectBase::FetchClassProperty(lua_State* L, UProperty* Property, void* Params, int32 Index)
{
	if (auto ClassProperty = Cast<UClassProperty>(Property))
	{
		ClassProperty->SetPropertyValue(Params, FLuaUClass::Fetch(L, Index));

		return true;
	}

	return false;
}

bool FLuaObjectBase::FetchObjectProperty(lua_State* L, UProperty* Property, void* Params, int32 Index)
{
	if (auto ObjectProperty = Cast<UObjectProperty>(Property))
	{
		ObjectProperty->SetObjectPropertyValue(Params, FLuaUObject::Fetch(L, Index));

		return true;
	}

	return false;
}

bool FLuaObjectBase::FetchArrayProperty(lua_State* L, UProperty* Property, void* Params, int32 Index)
{
	if (LUA_TTABLE != lua_type(L, Index))
	{
		//luaL_error(L, "Param %d is not a table!", Index);
		return false;
	}

	if (auto ArrayProperty = Cast<UArrayProperty>(Property))
	{
		FScriptArrayHelper ArrayHelper(ArrayProperty, Params);
		const int TableIndex = lua_absindex(L, Index);

		int32 Count = 0;
		lua_pushnil(L); // initial key, stack = [..., nil]
		while (lua_next(L, TableIndex))
		{
			if (ArrayHelper.Num() <= Count)
			{
				ArrayHelper.AddValue();
			}

			// stack = [..., key, value]
			FetchProperty(L, ArrayProperty->Inner, ArrayHelper.GetRawPtr(Count++), -1);

			lua_pop(L, 1); // stack = [..., key]
		}

		if (ArrayHelper.Num() > Count)
		{
			ArrayHelper.RemoveValues(Count, ArrayHelper.Num() - Count);
		}

		return true;
	}

	return false;
}

bool FLuaObjectBase::FetchSetProperty(lua_State* L, UProperty* Property, void* Params, int32 Index)
{
	if (LUA_TTABLE != lua_type(L, Index))
	{
		//luaL_error(L, "Param %d is not a table!", Index);
		return false;
	}

	if (auto SetProperty = Cast<USetProperty>(Property))
	{
		FScriptSetHelper SetHelper(SetProperty, Params);
		const int TableIndex = lua_absindex(L, Index);

		lua_pushnil(L);
		while (lua_next(L, TableIndex))
		{
			const int32 ElementIndex = SetHelper.AddDefaultValue_Invalid_NeedsRehash();
			FetchProperty(L, SetProperty->ElementProp, SetHelper.GetElementPtr(ElementIndex), -1);
			lua_pop(L, 1);
		}
		SetHelper.Rehash();

		return true;
	}

	return false;
}

bool FLuaObjectBase::FetchMapProperty(lua_State* L, UProperty* Property, void* Params, int32 Index)
{
	if (LUA_TTABLE != lua_type(L, Index))
	{
		//luaL_error(L, "Param %d is not a table!", Index);
		return false;
	}

	if (auto MapProperty = Cast<UMapProperty>(Property))
	{
		FScriptMapHelper MapHelper(MapProperty, Params);
		const int TableIndex = lua_absindex(L, Index);

		lua_pushnil(L);
		while (lua_next(L, TableIndex))
		{
			const int32 ElementIndex = MapHelper.AddDefaultValue_Invalid_NeedsRehash();

			uint8* PairPtr = MapHelper.GetPairPtr(ElementIndex);
			FetchProperty(L, MapProperty->ValueProp, PairPtr + MapProperty->MapLayout.ValueOffset, -1);
			FetchProperty(L, MapProperty->KeyProp, PairPtr/* + MapProperty->MapLayout.KeyOffset*/, -2);
			lua_pop(L, 1);
		}
		MapHelper.Rehash();

		return true;
	}

	return false;
}

bool FLuaObjectBase::FetchMulticastDelegateProperty(lua_State* L, UProperty* Property, void* Params, int32 Index)
{
#if ENGINE_MINOR_VERSION >= 23
	return false;
#else
	UMulticastDelegateProperty* DelegateProperty = Cast<UMulticastDelegateProperty>(Property);
	if (!DelegateProperty)
	{
		return false;
	}

	FScriptDelegate ScriptDelegate;
	if (!FLuaUDelegate::Fetch(L, Index, DelegateProperty->SignatureFunction, &ScriptDelegate))
	{
		return false;
	}

	FMulticastScriptDelegate* MulticastScriptDelegate = DelegateProperty->GetPropertyValuePtr(Params);
	if (MulticastScriptDelegate)
	{
		MulticastScriptDelegate->AddUnique(ScriptDelegate);
	}

	return true;
#endif // ENGINE_MINOR_VERSION >= 23
}

bool FLuaObjectBase::FetchMulticastInlineDelegateProperty(lua_State* L, UProperty* Property, void* Params, int32 Index)
{
#if ENGINE_MINOR_VERSION >= 23
	UMulticastInlineDelegateProperty* DelegateProperty = Cast<UMulticastInlineDelegateProperty>(Property);
	if (!DelegateProperty)
	{
		return false;
	}

	FScriptDelegate ScriptDelegate;
	if (!FLuaUDelegate::Fetch(L, Index, DelegateProperty->SignatureFunction, &ScriptDelegate))
	{
		return false;
	}

	FMulticastScriptDelegate* MulticastScriptDelegate = DelegateProperty->GetPropertyValuePtr(Params);
	if (MulticastScriptDelegate)
	{
		MulticastScriptDelegate->AddUnique(ScriptDelegate);
	}

	return true;
#else
	return false;
#endif // ENGINE_MINOR_VERSION >= 23
}

bool FLuaObjectBase::FetchMulticastSparseDelegateProperty(lua_State* L, UProperty* Property, void* Params, int32 Index)
{
#if ENGINE_MINOR_VERSION >= 23
	UMulticastSparseDelegateProperty* DelegateProperty = Cast<UMulticastSparseDelegateProperty>(Property);
	if (!DelegateProperty)
	{
		return false;
	}

	FScriptDelegate ScriptDelegate;
	if (!FLuaUDelegate::Fetch(L, Index, DelegateProperty->SignatureFunction, &ScriptDelegate))
	{
		return false;
	}

	DelegateProperty->AddDelegate(ScriptDelegate, nullptr, DelegateProperty->GetPropertyValuePtr(Params));

	return true;
#else
	return false;
#endif // ENGINE_MINOR_VERSION >= 23
}

bool FLuaObjectBase::FetchDelegateProperty(lua_State* L, UProperty* Property, void* Params, int32 Index)
{
	auto DelegateProperty = Cast<UDelegateProperty>(Property);
	if (!DelegateProperty)
	{
		return false;
	}

	FScriptDelegate* ScriptDelegate = DelegateProperty->GetPropertyValuePtr(Params);

	return FLuaUDelegate::Fetch(L, Index, DelegateProperty->SignatureFunction, ScriptDelegate);
}

bool FLuaObjectBase::Fetch(lua_State* L, int32 Index, int8& Value)
{
	Value = lua_tointeger(L, Index);

	return true;
}

bool FLuaObjectBase::Fetch(lua_State* L, int32 Index, uint8& Value)
{
	Value = lua_tointeger(L, Index);

	return true;
}

bool FLuaObjectBase::Fetch(lua_State* L, int32 Index, int16& Value)
{
	Value = lua_tointeger(L, Index);

	return true;
}

bool FLuaObjectBase::Fetch(lua_State* L, int32 Index, uint16& Value)
{
	Value = lua_tointeger(L, Index);

	return true;
}

bool FLuaObjectBase::Fetch(lua_State* L, int32 Index, int32& Value)
{
	Value = lua_tointeger(L, Index);

	return true;
}

bool FLuaObjectBase::Fetch(lua_State* L, int32 Index, uint32& Value)
{
	Value = lua_tointeger(L, Index);

	return true;
}

bool FLuaObjectBase::Fetch(lua_State* L, int32 Index, int64& Value)
{
	Value = lua_tointeger(L, Index);

	return true;
}

bool FLuaObjectBase::Fetch(lua_State* L, int32 Index, uint64& Value)
{
	Value = lua_tointeger(L, Index);

	return true;
}

bool FLuaObjectBase::Fetch(lua_State* L, int32 Index, float& Value)
{
	Value = lua_tonumber(L, Index);

	return true;
}

bool FLuaObjectBase::Fetch(lua_State* L, int32 Index, double& Value)
{
	Value = lua_tonumber(L, Index);

	return true;
}

bool FLuaObjectBase::Fetch(lua_State* L, int32 Index, bool& Value)
{
	Value = (lua_toboolean(L, Index) != 0);

	return true;
}

bool FLuaObjectBase::Fetch(lua_State* L, int32 Index, FString& Value)
{
	Value = UTF8_TO_TCHAR(lua_tostring(L, Index));

	return true;
}

bool FLuaObjectBase::Fetch(lua_State* L, int32 Index, FText& Value)
{
	Value = FText::FromString(UTF8_TO_TCHAR(lua_tostring(L, Index)));

	return true;
}

bool FLuaObjectBase::Fetch(lua_State* L, int32 Index, FName& Value)
{
	Value = UTF8_TO_TCHAR(lua_tostring(L, Index));

	return true;
}

int FLuaObjectBase::CallFunction(lua_State* L, UObject* Object, UFunction* Function, bool bIsParentDefaultFunction/* = false*/)
{
	uint8* Parms = (uint8*)FMemory_Alloca(Function->ParmsSize);
	FMemory::Memzero(Parms, Function->ParmsSize);

	int32 ParamIndex = 2;
	UProperty* ReturnValue = nullptr;
	for (TFieldIterator<UProperty> ParamIter(Function); ParamIter && (ParamIter->PropertyFlags & CPF_Parm); ++ParamIter)
	{
		UProperty* ParamProperty = *ParamIter;
		if (!ParamProperty->HasAnyPropertyFlags(CPF_ZeroConstructor))
		{
			ParamProperty->InitializeValue_InContainer(Parms);
		}

		if (ParamProperty->PropertyFlags & CPF_ReturnParm)
		{
			ReturnValue = ParamProperty;
		}
		else
		{
			FetchProperty(L, ParamProperty, ParamProperty->ContainerPtrToValuePtr<uint8>(Parms), ParamIndex++);
		}
	}

	const EFunctionFlags FunctionFlags = Function->FunctionFlags;
	const FNativeFuncPtr NativeFucPtr = Function->GetNativeFunc();

	if (NativeFucPtr == &ILuaImplementableInterface::ProcessBPFunctionOverride)
	{
		ILuaImplementableInterface* LuaObject = Cast<ILuaImplementableInterface>(Object);
		const bool bOverride = LuaObject ? LuaObject->HasBPFunctionOverrding(Function->GetName()) : false;

		if (!bOverride || bIsParentDefaultFunction)
		{
			Function->FunctionFlags &= ~FUNC_Native;
			Function->SetNativeFunc(&UObject::ProcessInternal);
		}
	}

	if (bIsParentDefaultFunction)
	{
		Object->UObject::ProcessEvent(Function, Parms);
	}
	else
	{
		Object->ProcessEvent(Function, Parms);
	}

	Function->FunctionFlags = FunctionFlags;
	Function->SetNativeFunc(NativeFucPtr);

	int32 ReturnNum = 0;
	if (ReturnValue)
	{
		FLuaObjectBase::PushProperty(L, ReturnValue, ReturnValue->ContainerPtrToValuePtr<uint8>(Parms));
		ReturnNum++;
	}

	for (TFieldIterator<UProperty> ParamIter(Function); ParamIter && (ParamIter->PropertyFlags & CPF_Parm); ++ParamIter)
	{
		if (!(ParamIter->PropertyFlags & CPF_ReturnParm) &&
			(ParamIter->PropertyFlags & (CPF_ConstParm | CPF_OutParm)) == CPF_OutParm)
		{
			FLuaObjectBase::PushProperty(L, *ParamIter, (*ParamIter)->ContainerPtrToValuePtr<uint8>(Parms));
			ReturnNum++;
		}

		ParamIter->DestroyValue_InContainer(Parms);
	}

	return ReturnNum;
}
