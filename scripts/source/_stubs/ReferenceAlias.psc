Scriptname ReferenceAlias extends Alias Hidden
{Stub. Declarations only - see README.txt in this folder.

The Alias base class ships with SKSE in full, so RegisterForSleep(),
OnSleepStart/Stop, RegisterForSingleUpdate and OnUpdate are inherited and
need not be declared here.

Only what the mod actually uses is listed. Add a new event one at a time and
verify its signature - a wrong signature compiles silently and then never
fires.}

; -- reference access --------------------------------------------------

ObjectReference Function GetReference() native
Actor Function GetActorReference() native
Function ForceRefTo(ObjectReference akNewRef) native
Function ForceRefIfEmpty(ObjectReference akNewRef) native
Function Clear() native

; -- events ----------------------------------------------------------

; Equipping an item. For ALCH (food/potions) it fires on use - the hunger
; hook depends on this.
Event OnObjectEquipped(Form akBaseObject, ObjectReference akReference)
EndEvent

; Signature confirmed against SkyUI: SKI_PlayerLoadGameAlias.psc
Event OnPlayerLoadGame()
EndEvent

Event OnDeath(Actor akKiller)
EndEvent

Event OnLocationChange(Location akOldLoc, Location akNewLoc)
EndEvent
