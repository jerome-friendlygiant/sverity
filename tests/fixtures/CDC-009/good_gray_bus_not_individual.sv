// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect-none: CDC-009
// Near miss of bad_bus_bits_synced_individually.sv: the arbitrary 2-bit bus is replaced by a Gray-coded counter, which is exempt from CDC-009 (CDC-018 reports its unproven skew).
module top(input logic clka, clkb, rst_n, inc, output logic [1:0] q);
  logic [1:0] bin, gray, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) bin <= '0; else if (inc) bin <= bin + 1;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) gray <= '0; else gray <= bin ^ (bin >> 1);
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s2, s1} <= '0; else {s2, s1} <= {s1, gray};
  assign q = s2;
endmodule
