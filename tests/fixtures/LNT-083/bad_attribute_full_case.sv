// sverity-args: --top top
// The attribute spelling of full_case is read like the comment: synthesis treats the missing selector values as don't care.
// expect: LNT-083 WARNING 7
module top(input logic a, b, input logic [1:0] s, output logic y);
  always_comb begin
    y = 1'b0;
    (* synthesis, full_case *)
    case (s)
      2'd0: y = a;
      2'd1: y = b;
    endcase
  end
endmodule
