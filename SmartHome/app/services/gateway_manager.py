import os

class GatewayManager:
    _instance = None
    _cache_file = "gw_ip.cache"
    
    def __new__(cls):
        if cls._instance is None:
            cls._instance = super(GatewayManager, cls).__new__(cls)
            cls._instance.active_ip = "192.168.1.100"
            cls._instance.discovered_nodes = {}
            cls._instance._load_ip()
        return cls._instance

    def _load_ip(self):
        if os.path.exists(self._cache_file):
            try:
                with open(self._cache_file, "r") as f:
                    saved_ip = f.read().strip()
                    if saved_ip:
                        self.active_ip = saved_ip
            except Exception:
                pass

    def update_ip(self, new_ip: str):
        if new_ip and new_ip != self.active_ip:
            self.active_ip = new_ip
            try:
                with open(self._cache_file, "w") as f:
                    f.write(new_ip)
            except Exception:
                pass

# Khởi tạo instance dùng chung toàn hệ thống
gateway = GatewayManager()