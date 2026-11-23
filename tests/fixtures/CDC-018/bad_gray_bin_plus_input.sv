// sverity-args: --top top --sdc @DIR@/skew.sdc
// An unconstrained runtime addend can advance the binary source by multiple steps.
// expect: CDC-018 WARNING
// expect-unresolved: CDC-018
// expect-none: CDC-017
module top(input logic clka, clkb, rst_n, input logic [3:0] step, output logic [3:0] q);
  logic [3:0] bin, gray, s1, s2;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) bin <= '0;
    else bin <= bin + step;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) gray <= '0;
    else gray <= bin ^ (bin >> 1);
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) {s2, s1} <= '0;
    else {s2, s1} <= {s1, gray};
  assign q = s2;
endmodule
