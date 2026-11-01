// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A clock created by logic is a generated/gated clock whose relationship is unresolved.
// expect: CDC-002 WARNING
// expect-coverage: clock-extraction INCOMPLETE
module top(input logic clka, en, rst_n, d, output logic q);
  logic gclk;
  assign gclk = clka & en;
  always_ff @(posedge gclk or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
