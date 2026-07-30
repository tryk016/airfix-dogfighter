#import "AirfixControllerInputProfileCoordinator.h"

#import "AirfixIOSControllerInputProfileStore.h"

@interface AirfixControllerInputProfileCoordinator () {
  __strong AirfixIOSControllerInputProfileStore *_store;
  airfix::input::ControllerInputProfileRecord _activeProfile;
  airfix::input::ControllerInputProfileRecord _persistentProfile;
}

@property(nonatomic, readwrite) BOOL persistenceAvailable;
@property(nonatomic, readwrite) BOOL repairRequired;
@property(nonatomic, readwrite, getter=isSaving) BOOL saving;

@end

@implementation AirfixControllerInputProfileCoordinator

- (nullable instancetype)
           initWithStore:(AirfixIOSControllerInputProfileStore *)store
           activeProfile:(const airfix::input::ControllerInputProfileRecord &)
                             activeProfile
       persistentProfile:(const airfix::input::ControllerInputProfileRecord &)
                             persistentProfile
    persistenceAvailable:(BOOL)persistenceAvailable
          repairRequired:(BOOL)repairRequired {
  NSParameterAssert(store != nil);
  self = [super init];
  if (self == nil) {
    return nil;
  }
  if (store == nil ||
      !airfix::input::resolveControllerInputProfile(activeProfile).complete() ||
      !airfix::input::resolveControllerInputProfile(persistentProfile)
           .complete()) {
    return nil;
  }
  _store = store;
  _activeProfile = activeProfile;
  _persistentProfile = persistentProfile;
  _persistenceAvailable = persistenceAvailable;
  _repairRequired = persistenceAvailable && repairRequired;
  return self;
}

- (airfix::input::ControllerInputProfileRecord)activeProfile {
  NSAssert(NSThread.isMainThread, @"Controller profile state belongs to main");
  return _activeProfile;
}

- (airfix::input::ControllerInputProfileRecord)persistentProfile {
  NSAssert(NSThread.isMainThread, @"Controller profile state belongs to main");
  return _persistentProfile;
}

- (void)requestPersistentProfile:
            (const airfix::input::ControllerInputProfileRecord &)profile
                      completion:
                          (AirfixControllerInputProfileSaveResultCompletion)
                              completion {
  NSAssert(NSThread.isMainThread, @"Controller profile saving belongs to main");
  NSParameterAssert(completion != nil);

  const auto resolved = airfix::input::resolveControllerInputProfile(profile);
  if (!resolved.complete()) {
    completion(AirfixControllerInputProfileSaveResultInvalidCandidate);
    return;
  }
  if (!self.persistenceAvailable || _store == nil) {
    completion(AirfixControllerInputProfileSaveResultPersistenceUnavailable);
    return;
  }
  if (self.saving) {
    completion(AirfixControllerInputProfileSaveResultBusy);
    return;
  }

  const airfix::input::ControllerInputProfileRecord candidate = profile;
  self.saving = YES;
  AirfixControllerInputProfileCoordinator *owner = self;
  [_store
      saveProfile:candidate
       completion:^(
           const airfix::ios::ControllerInputProfileSaveOutcome outcome) {
         NSAssert(NSThread.isMainThread,
                  @"Controller profile completion belongs to main");
         owner.saving = NO;
         if (outcome.durable &&
             outcome.error ==
                 airfix::ios::ControllerInputProfileStorageError::none) {
           owner->_persistentProfile = candidate;
           owner.repairRequired = NO;
           completion(AirfixControllerInputProfileSaveResultSaved);
           return;
         }

         switch (outcome.error) {
         case airfix::ios::ControllerInputProfileStorageError::invalidProfile:
           completion(AirfixControllerInputProfileSaveResultInvalidCandidate);
           return;
         case airfix::ios::ControllerInputProfileStorageError::
             storageUnavailable:
         case airfix::ios::ControllerInputProfileStorageError::
             persistenceBlocked:
           owner.persistenceAvailable = NO;
           owner.repairRequired = NO;
           completion(
               AirfixControllerInputProfileSaveResultPersistenceUnavailable);
           return;
         case airfix::ios::ControllerInputProfileStorageError::none:
         case airfix::ios::ControllerInputProfileStorageError::saveFailed:
         case airfix::ios::ControllerInputProfileStorageError::commitUnknown:
           completion(AirfixControllerInputProfileSaveResultSaveFailed);
           return;
         }
         completion(AirfixControllerInputProfileSaveResultSaveFailed);
       }];
}

@end
