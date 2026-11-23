// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Exact combinational alias on the delayed stage operand still forms an effective XOR detector.
// expect: CDC-014 INFO
// expect-none: CDC-014 WARNING
module top(input logic clka, clkb, rst_n, pulse_i, output logic event_o);
  logic toggle_q, s1, s2, s3, d_next;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) toggle_q <= 1'b0;
    else if (pulse_i) toggle_q <= ~toggle_q;
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) {s3, s2, s1} <= '0;
    else {s3, s2, s1} <= {s2, s1, toggle_q};
  assign d_next = s3;
  assign event_o = s2 ^ d_next;
endmodule
