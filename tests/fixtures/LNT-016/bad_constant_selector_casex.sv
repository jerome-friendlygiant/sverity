// sverity-args: --top top
// casex syntax is flagged even when elaboration can resolve the selector.
// expect: LNT-016 WARNING 6
module top(output logic y);
  always_comb begin
    casex (1'b0)
      1'b0: y = 1'b0;
      default: y = 1'b1;
    endcase
  end
endmodule
