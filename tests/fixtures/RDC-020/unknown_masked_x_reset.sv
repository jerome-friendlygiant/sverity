// sverity-args: --top top
// q & 2'bx0 clears bit 0 to 0 while leaving bit 1 undecided ('?'): RDC-020 and RDC-025 must both report Unresolved
// for the partially reset vector (round 4 A2a mask case).
// expect-unresolved: RDC-020
// expect-unresolved: RDC-025
module top(input logic clk, rst_n, input logic [1:0] d, output logic [1:0] q);
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) q <= q & 2'bx0;
    else q <= d;
endmodule
