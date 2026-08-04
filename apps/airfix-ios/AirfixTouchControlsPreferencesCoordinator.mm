#import "AirfixTouchControlsPreferencesCoordinator.h"

#import "AirfixIOSTouchControlsPreferencesStore.h"
#import "AirfixTouchControlsView.h"

@interface AirfixTouchControlsPreferencesCoordinator () {
  __strong AirfixIOSTouchControlsPreferencesStore *_store;
  __weak AirfixTouchControlsView *_touchControlsView;
  airfix::input::TouchControlsPreferences _activePreferences;
}

@property(nonatomic, readwrite) BOOL persistenceAvailable;
@property(nonatomic, readwrite) BOOL repairRequired;
@property(nonatomic, readwrite, getter=isSaving) BOOL saving;

@end

@implementation AirfixTouchControlsPreferencesCoordinator

- (nullable instancetype)
           initWithStore:(AirfixIOSTouchControlsPreferencesStore *)store
       touchControlsView:(AirfixTouchControlsView *)touchControlsView
       activePreferences:
           (const airfix::input::TouchControlsPreferences &)preferences
    persistenceAvailable:(BOOL)persistenceAvailable
          repairRequired:(BOOL)repairRequired {
  NSParameterAssert(store != nil);
  NSParameterAssert(touchControlsView != nil);
  self = [super init];
  if (self == nil) {
    return nil;
  }
  if (store == nil || touchControlsView == nil ||
      airfix::input::validateTouchControlsPreferences(preferences)
          .has_value() ||
      ![touchControlsView applyPreferences:preferences]) {
    return nil;
  }
  _store = store;
  _touchControlsView = touchControlsView;
  _activePreferences = preferences;
  _persistenceAvailable = persistenceAvailable;
  _repairRequired = persistenceAvailable && repairRequired;
  return self;
}

- (airfix::input::TouchControlsPreferences)activePreferences {
  NSAssert(NSThread.isMainThread, @"Touch preferences belong to main");
  return _activePreferences;
}

- (void)requestPreferences:
            (const airfix::input::TouchControlsPreferences &)preferences
                completion:(AirfixTouchControlsPreferencesSaveResultCompletion)
                               completion {
  NSAssert(NSThread.isMainThread, @"Touch preference saving belongs to main");
  NSParameterAssert(completion != nil);
  if (airfix::input::validateTouchControlsPreferences(preferences)
          .has_value()) {
    completion(AirfixTouchControlsPreferencesSaveResultInvalidCandidate);
    return;
  }
  if (!self.persistenceAvailable || _store == nil) {
    completion(AirfixTouchControlsPreferencesSaveResultPersistenceUnavailable);
    return;
  }
  if (self.saving) {
    completion(AirfixTouchControlsPreferencesSaveResultBusy);
    return;
  }

  const airfix::input::TouchControlsPreferences candidate = preferences;
  self.saving = YES;
  AirfixTouchControlsPreferencesCoordinator *owner = self;
  [_store
      savePreferences:candidate
           completion:^(
               const airfix::ios::TouchControlsPreferencesSaveOutcome outcome) {
             NSAssert(NSThread.isMainThread,
                      @"Touch preference completion belongs to main");
             owner.saving = NO;
             if (outcome.durable &&
                 outcome.error ==
                     airfix::ios::TouchControlsPreferencesStorageError::none) {
               AirfixTouchControlsView *view = owner->_touchControlsView;
               if (view != nil && [view applyPreferences:candidate]) {
                 owner->_activePreferences = candidate;
                 owner.repairRequired = NO;
                 completion(AirfixTouchControlsPreferencesSaveResultSaved);
                 return;
               }
               completion(
                   AirfixTouchControlsPreferencesSaveResultInvalidCandidate);
               return;
             }

             switch (outcome.error) {
             case airfix::ios::TouchControlsPreferencesStorageError::
                 invalidPreferences:
               completion(
                   AirfixTouchControlsPreferencesSaveResultInvalidCandidate);
               return;
             case airfix::ios::TouchControlsPreferencesStorageError::
                 storageUnavailable:
             case airfix::ios::TouchControlsPreferencesStorageError::
                 persistenceBlocked:
               owner.persistenceAvailable = NO;
               owner.repairRequired = NO;
               completion(
                   AirfixTouchControlsPreferencesSaveResultPersistenceUnavailable);
               return;
             case airfix::ios::TouchControlsPreferencesStorageError::none:
             case airfix::ios::TouchControlsPreferencesStorageError::saveFailed:
             case airfix::ios::TouchControlsPreferencesStorageError::
                 commitUnknown:
               completion(AirfixTouchControlsPreferencesSaveResultSaveFailed);
               return;
             }
             completion(AirfixTouchControlsPreferencesSaveResultSaveFailed);
           }];
}

@end
