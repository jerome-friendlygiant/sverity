// sverity-args: --top top
// a constant case expression with a non-constant item: the reachable arms cannot be decided (Unresolved WARNING)
// expect: LNT-018 WARNING 7
module top(input logic a, input logic [1:0] s, output logic y);
  always_comb begin
    y = a;
    case (2'd1)
      s: y = ~a;
      default: y = 1'b0;
    endcase
  end
endmodule
