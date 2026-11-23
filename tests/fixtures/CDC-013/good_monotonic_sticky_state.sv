// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A sticky source flag can rise once but cannot produce a short pulse.
// expect-none: CDC-013 WARNING
// expect-none: CDC-014 WARNING
module top(input logic clka, clkb, set_i, output logic seen_o);
  logic sticky_q, s1, s2;
  always_ff @(posedge clka) sticky_q <= sticky_q | set_i;
  always_ff @(posedge clkb) begin s1 <= sticky_q; s2 <= s1; end
  assign seen_o = s2;
endmodule
