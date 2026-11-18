// sverity-args: --top top
// The force sits in a generate branch this configuration does not elaborate, so it is not hardware of this run.
// What the run reports instead: LNT-051 INFO names the branch; LNT-079 stays silent for it.
// expect: LNT-051 INFO 7
// expect-none: LNT-079
module top #(parameter bit SEL = 0) (input logic clk, input logic d, output logic q);
  if (SEL) begin : g_on
    always @(posedge clk) force q = d;
  end else begin : g_off
    always_ff @(posedge clk) q <= d;
  end
endmodule
