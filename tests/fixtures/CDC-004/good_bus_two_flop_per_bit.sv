// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect-none: CDC-004
// Near miss of bad_bus_no_sync.sv: the single capture register in clkb is replaced by a 2-flop synchroniser per bit.
module top(input logic clka, clkb, rst_n, input logic [3:0] d, output logic [3:0] q);
  logic [3:0] a_q, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= '0; else a_q <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s2, s1} <= '0; else {s2, s1} <= {s1, a_q};
  assign q = s2;
endmodule
