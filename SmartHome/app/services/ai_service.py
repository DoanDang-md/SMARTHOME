import json
import asyncio
from google.antigravity import Agent, LocalAgentConfig
import os 
from dotenv import load_dotenv

load_dotenv()

class SmartHomeAI:
    def __init__(self):
        # API Key của bạn lấy từ bảng điều khiển Antigravity
        api_key = os.getenv("GEMINI_API_KEY")

    async def analyze_command_async(self, text_input: str, user_devices: list) -> dict:
        """
        Hàm xử lý bất đồng bộ (async) chuẩn của Antigravity SDK
        """
        device_context = "\n".join([
            f"- ID: {d.id} | Tên: {d.name} | Trạng thái hiện tại: {'Đang BẬT' if d.status == 1 else 'Đang TẮT'}" 
            for d in user_devices
        ])

        if not device_context:
            device_context = "Người dùng này chưa có thiết bị nào."

        system_instructions = f"""
        Bạn là quản gia thông minh của hệ thống NexusHome. Dưới đây là danh sách thiết bị người dùng đang có quyền điều khiển:
        {device_context}

        Nhiệm vụ của bạn:
        1. Phân tích ngữ nghĩa xem người dùng muốn bật hay tắt thiết bị nào. 
        2. BẮT BUỘC TRẢ VỀ CHỈ MỘT CHUỖI JSON DUY NHẤT (Không bọc bằng ký hiệu markdown, không giải thích), format:
        {{
            "action": "ON" hoặc "OFF" hoặc "CHAT",
            "device_id": <Điền số ID của thiết bị. Nếu không tìm thấy điền null>,
            "reply": "<Một câu trả lời tự nhiên bằng tiếng Việt>"
        }}
        """

        # Cấu hình Agent với API Key và System Instructions
        config = LocalAgentConfig(
            api_key=self.api_key,
            system_instructions=system_instructions
        )

        try:
            # Gọi Antigravity Agent
            async with Agent(config) as agent:
                response = await agent.chat(text_input)
                response_text = await response.text()
                
                # Xử lý chuỗi JSON
                clean_text = response_text.strip().strip('`')
                if clean_text.startswith('json'):
                    clean_text = clean_text[4:]
                    
                return json.loads(clean_text)
                
        except Exception as e:
            print(f"[AI Error] Lỗi phân tích Antigravity: {e}")
            return {"action": "CHAT", "device_id": None, "reply": "Xin lỗi anh Đăng, não bộ Antigravity của em đang bảo trì ạ!"}

    def analyze_command(self, text_input: str, user_devices: list) -> dict:
        """
        Hàm bọc đồng bộ (sync) để gọi dễ dàng từ telegram_service.py
        """
        return asyncio.run(self.analyze_command_async(text_input, user_devices))