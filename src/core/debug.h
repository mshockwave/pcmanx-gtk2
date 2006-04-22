#ifdef USE_DEBUG
#define DEBUG(format...) g_log(G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, format)
#define INFO_ON(format...) g_log(G_LOG_DOMAIN, G_LOG_LEVEL_INFO, format)
#else
#define DEBUG(format...)
#define INFO_ON(format...)
#endif

#if 0
#define INFO(format...) printf(format);
#else
#define INFO(format...)
#endif
