#ifndef RTL_433_EXPORT_H
#define RTL_433_EXPORT_H

#if defined __GNUC__
#  if __GNUC__ >= 4
#    define __RTL_433_EXPORT   __attribute__((visibility("default")))
#    define __RTL_433_IMPORT   __attribute__((visibility("default")))
#  else
#    define __RTL_433_EXPORT
#    define __RTL_433_IMPORT
#  endif
#elif _MSC_VER
#  define __RTL_433_EXPORT     __declspec(dllexport)
#  define __RTL_433_IMPORT     __declspec(dllimport)
#else
#  define __RTL_433_EXPORT
#  define __RTL_433_IMPORT
#endif

#ifndef librtl_433_STATIC
#	ifdef librtl_433_EXPORTS
#	define RTL_433_API __RTL_433_EXPORT
#	else
#	define RTL_433_API __RTL_433_IMPORT
#	endif
#else
#define RTL_433_API
#endif
#endif /* RTL_433_EXPORT_H */
