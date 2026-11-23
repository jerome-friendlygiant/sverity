// sverity-args: --top top --sdc @DIR@/primary_input_async.sdc
// expect: CDC-026 ERROR
// expect-none: CDC-029 INFO
// expect-unresolved: CDC-029
module top(input logic clka, clkb, rst_a_n, rst_b_n, d, output logic q);
  logic s1;
  always_ff @(posedge clka) begin end
  always_ff @(posedge clkb or negedge rst_a_n) if (!rst_a_n) s1 <= 1'b0; else s1 <= d;
  always_ff @(posedge clkb or negedge rst_b_n) if (!rst_b_n) q <= 1'b0; else q <= s1;
endmodule
