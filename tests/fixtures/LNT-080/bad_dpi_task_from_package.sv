// sverity-args: --top top
// A context task declared in a package and called from design logic: reported at the declaration, with the call.
// expect: LNT-080 WARNING 6
// expect-none: LNT-013
package model_pkg;
  import "DPI-C" context task sample(output int unsigned v);
endpackage
module top(input logic clk, output logic [31:0] q);
  int unsigned v;
  always_ff @(posedge clk) begin
    model_pkg::sample(v);
    q <= v;
  end
endmodule
