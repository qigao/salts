#ifndef TURBO_CAPTURE_IOS_GUARD_H
#define TURBO_CAPTURE_IOS_GUARD_H

#import <Foundation/Foundation.h>

typedef void (*turbo_ios_capture_finalizer)(void *capture);

@interface TurboCaptureGuard : NSObject

- (instancetype)initWithCapture:(void *)capture
                       finalizer:(turbo_ios_capture_finalizer)finalizer;
- (void *)acquireCapture;
- (void)releaseCapture;
- (void)detachOwner;

@end

#endif
