// sverity-args: --top top --sdc @DIR@/reconvergence_async_bits.sdc
// expect: CDC-010 WARNING
// expect: CDC-026 ERROR
module top(input logic src_clk, dst_clk, rst_n, rst2_n, input logic [1:0] din, output logic q);
  logic [1:0] s1, s2;
  always_ff @(posedge dst_clk or negedge rst_n) if (!rst_n) s1 <= 0; else s1 <= din;
  always_ff @(posedge dst_clk or negedge rst2_n) if (!rst2_n) s2 <= 0; else s2 <= s1;
  always_ff @(posedge dst_clk) q <= s2[0] & s2[1];
endmodule
