// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect-none: CDC-008
// Near miss of bad_logic_before_sync.sv: the combinational AND in front of the synchroniser is removed; the source is the registered flop a1.
module top(input logic clka, clkb, rst_n, d, e, output logic q);
  logic a1, a2, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) {a2, a1} <= 2'b00; else {a2, a1} <= {e, d};
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s2, s1} <= 2'b00; else {s2, s1} <= {s1, a1};
  assign q = s2;
endmodule
