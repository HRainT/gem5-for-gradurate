import torch
import numpy as np
import os
import sys
import yaml
import re
from collections import OrderedDict

# --------------------- 配置区域 ---------------------
TEST_PC = "0x1a2e8"  # 要测试的分支PC地址
MODEL_DIR = "/Data3/yutong.han/My_G5Project/BranchNet/testrun5/473.astar/checkpoints"
SRC_DIR = "/Data3/yutong.han/My_G5Project/BranchNet/src/branchnet"
CONFIG_FILE = os.path.join(SRC_DIR, "configs/mini_250.yaml")
# --------------------------------------------------

# 添加原始模型代码路径
sys.path.append(SRC_DIR)

# 导入原始模型定义
from model import BranchNet, BranchNetTrainingPhaseKnobs
from dataset_loader import preprocess_history

class BranchNetTester:
    def __init__(self, pc, model_dir, config_path):
        self.pc = pc
        self.model_dir = model_dir
        self.model = None
        self.feature_dim = None
        self.config = None
        self.history_length = None
        
        print(f"Loading model for PC: {pc}")
        self.load_config(config_path)
        self.load_model()
    
    def load_config(self, config_path):
        """加载模型配置文件"""
        with open(config_path, 'r') as f:
            self.config = yaml.safe_load(f)
        
        # 计算所需历史长度
        self.history_length = max(self.config['history_lengths'])
        if any(self.config['shifting_pooling']):
            self.history_length += max(self.config['pooling_widths'])
        
        # 设置特征维度
        self.feature_dim = self.history_length
        print(f"Loaded config from {config_path}")
        print(f"History length required: {self.history_length}")
    
    def load_model(self):
        """加载原始模型"""
        model_files = [
            f"base_{self.pc}_checkpoint.pt",
            f"final_{self.pc}_checkpoint.pt",
            f"frozen_sumpooling_{self.pc}_checkpoint.pt",
            f"lut_conv_{self.pc}_checkpoint.pt",
            f"pruned_fc_layers_{self.pc}_checkpoint.pt"
        ]
        
        # 找到存在的模型文件
        model_path = None
        for model_file in model_files:
            candidate = os.path.join(self.model_dir, model_file)
            if os.path.exists(candidate):
                model_path = candidate
                print(f"Found model: {model_path}")
                break
        
        if not model_path:
            raise FileNotFoundError(f"No model found for PC {self.pc}")
        
        # 创建训练阶段配置
        knobs = BranchNetTrainingPhaseKnobs()
        
        # 根据模型文件名设置特定配置
        if "final" in model_path:
            knobs.lut_convolution = True
            knobs.quantize_sumpooling = True
            knobs.quantize_hidden_fc = True
            knobs.prune_fc_layers = True
            knobs.freeze_sumpooling_batchnorm_params = True
            knobs.freeze_hidden_fc_params = True
        elif "frozen" in model_path:
            knobs.quantize_sumpooling = True
            knobs.freeze_sumpooling_batchnorm_params = True
        elif "lut" in model_path:
            knobs.lut_convolution = True
        elif "pruned" in model_path:
            knobs.prune_fc_layers = True
        
        # 从原始代码创建模型
        self.model = BranchNet(self.config, knobs)
        
        # 加载检查点
        checkpoint = torch.load(model_path, map_location='cpu')
        
        # 处理不同的检查点格式
        if 'model_state_dict' in checkpoint:
            state_dict = checkpoint['model_state_dict']
        else:
            state_dict = checkpoint
        
        # 修复状态字典键名
        fixed_state_dict = OrderedDict()
        for k, v in state_dict.items():
            # 修复可能的键名不一致问题
            if k.startswith("module."):
                fixed_key = k[7:]
            else:
                fixed_key = k
            fixed_state_dict[fixed_key] = v
        
        # 加载状态字典
        self.model.load_state_dict(fixed_state_dict)
        self.model.eval()
        print(f"Model loaded successfully")
    
    def preprocess_feature(self, raw_feature):
        """预处理特征向量以匹配训练时的处理"""
        # 转换为numpy数组
        if not isinstance(raw_feature, np.ndarray):
            raw_feature = np.array(raw_feature, dtype=np.int64)
        
        # 应用与训练时相同的预处理
        processed = preprocess_history(
            raw_feature,
            pc_bits=self.config['pc_bits'],
            pc_hash_bits=self.config['pc_hash_bits'],
            hash_dir_with_pc=self.config['hash_dir_with_pc'],
            dtype=np.int64
        )
        return processed
    
    def predict(self, feature_vector):
        """
        使用模型进行预测
        :param feature_vector: 原始输入特征向量 (1D 列表/数组)
        :return: (prediction, confidence)
        """
        # 预处理特征
        processed_feature = self.preprocess_feature(feature_vector)
        
        # 检查特征长度
        if len(processed_feature) < self.history_length:
            raise ValueError(f"Feature too short. Expected at least {self.history_length}, got {len(processed_feature)}")
        
        # 使用最后的历史部分
        if len(processed_feature) > self.history_length:
            processed_feature = processed_feature[-self.history_length:]
        
        # 转换为张量并添加批次维度
        input_tensor = torch.tensor(processed_feature, dtype=torch.int64).unsqueeze(0)
        
        with torch.no_grad():
            output = self.model(input_tensor)
            
            # 修复：处理不同类型的模型输出
            if isinstance(output, tuple):
                # 如果输出是元组，取第一个元素
                output = output[0]
                
            if output.dim() == 1:
                # 一维输出 (batch_size,)
                prob_taken = torch.sigmoid(output).item()
                prediction = prob_taken > 0.5
            elif output.dim() == 2:
                # 二维输出 (batch_size, 1)
                if output.shape[1] == 1:
                    prob_taken = torch.sigmoid(output).item()
                    prediction = prob_taken > 0.5
                else:
                    # 二维输出 (batch_size, 2) - 使用softmax
                    prob = torch.softmax(output, dim=1)
                    prob_taken = prob[0, 1].item()
                    prediction = prob_taken > 0.5
            else:
                # 其他维度输出，取最后一个元素
                prob_taken = torch.sigmoid(output[-1]).item()
                prediction = prob_taken > 0.5
        
        return prediction, prob_taken
    
    def generate_test_feature(self):
        """生成测试用特征向量"""
        # 生成符合配置的随机特征
        pc_mask = (1 << (self.config['pc_bits'] + 1)) - 1
        return np.random.randint(0, pc_mask + 1, size=self.history_length).tolist()


if __name__ == "__main__":
    # 初始化测试器
    tester = BranchNetTester(TEST_PC, MODEL_DIR, CONFIG_FILE)
    
    # 生成测试特征
    test_feature = tester.generate_test_feature()
    print(f"\nGenerated test feature (first 10 values): {test_feature[:10]}")
    
    # 进行预测
    prediction, confidence = tester.predict(test_feature)
    
    # 输出结果
    print(f"\nPrediction for PC {TEST_PC}:")
    print(f"  Direction: {'TAKEN' if prediction else 'NOT TAKEN'}")
    print(f"  Confidence: {confidence:.4f}")
    print(f"  Probability TAKEN: {confidence:.2%}")
    print(f"  Probability NOT TAKEN: {(1 - confidence):.2%}")
