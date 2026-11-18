// sverity-args: --top top
// The casex sits in a generate branch this configuration does not elaborate, so it is not hardware of this run.
// What the run reports instead: LNT-051 INFO names the branch; LNT-016 stays silent for it.
// expect: LNT-051 INFO 7
// expect-none: LNT-016
module top #(parameter bit SEL = 0) (input logic [1:0] s, output logic y);
  if (SEL) begin : g_on
    always_comb begin
      casex (s)
        2'b1x: y = 1'b1;
        default: y = 1'b0;
      endcase
    end
  end else begin : g_off
    assign y = s[0];
  end
endmodule
