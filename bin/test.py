from torchinfo import summary  # pip install torchinfo
model = build_your_branchnet()  # 不 .to(cuda)
summary(model, depth=3)         # 打印参数量

total_params = sum(p.numel() for p in model.parameters())
print(f'{total_params/1e6:.1f} M params,  FP32 size ≈ {total_params*4/1e6:.1f} MB')