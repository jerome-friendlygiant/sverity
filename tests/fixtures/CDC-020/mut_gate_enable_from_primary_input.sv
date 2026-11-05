// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// MUTATION of a clock gate: its enable is a primary input with no register in the clock domain.
// expect: CDC-020 WARNING
// revert: assign gclk = clka & en_in; => logic en; always_ff @(posedge clka or negedge rst_n) if (!rst_n) en <= 1'b0; else en <= en_in; assign gclk = clka & en;
// revert-expect-none: CDC-020 WARNING
module top(input logic clka, rst_n, en_in, d, output logic q);
  logic gclk;
  assign gclk = clka & en_in;
  always_ff @(posedge gclk or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
