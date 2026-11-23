// sverity-args: --top top --sdc @DIR@/same_root_active_names.sdc
// One RTL root has two active clock names, so even its self-comparison must remain Unknown.
// expect: CDC-003 WARNING
// expect-unresolved: CDC-003
// expect-coverage: clock-relationships INCOMPLETE
module top(input logic clk);
  logic q_src, q_dst;
  always_ff @(posedge clk) q_src <= ~q_src;
  always_ff @(posedge clk) q_dst <= q_src;
endmodule
