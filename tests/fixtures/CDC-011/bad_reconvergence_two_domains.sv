// sverity-args: --top top --sdc @DIR@/three.sdc
// revert: q <= sa2 | sc2; => q <= sa2;
// revert-expect-none: CDC-011 WARNING
// expect: CDC-011 WARNING
module top(input logic clka, clkc, clkb, rst_n, d1, d2, output logic q);
  logic a1, c1, sa1, sa2, sc1, sc2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a1 <= 1'b0; else a1 <= d1;
  always_ff @(posedge clkc or negedge rst_n) if (!rst_n) c1 <= 1'b0; else c1 <= d2;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {sa2, sa1, sc2, sc1} <= '0; else {sa2, sa1, sc2, sc1} <= {sa1, a1, sc1, c1};
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= sa2 | sc2;
endmodule
