// sverity-args: --top top
// The second writer sits under a parameter condition that is false in this configuration, so it never runs.
// expect-none: LNT-049 WARNING
module top #(parameter bit Extra = 1'b0) (input logic clk, we, input logic [3:0] d, output logic [3:0] q);
  logic [3:0] m [4];
  always_ff @(posedge clk) if (we) m[1] <= d;
  always_ff @(posedge clk) if (Extra) m[1] <= ~d;
  assign q = m[1];
endmodule
