module top(input logic src_clk,
  dst_clk,
  irq,
  output logic irq_dst);

  logic irq_src;
  always_ff @(posedge src_clk) begin
    irq_src <= irq;
  end
  always_ff @(posedge dst_clk) begin
    irq_dst <= irq_src;
  end

endmodule
