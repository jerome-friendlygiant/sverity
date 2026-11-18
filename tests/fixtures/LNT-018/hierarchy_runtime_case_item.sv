// sverity-args: --top top
// a constant case expression whose only item is a runtime value: which arm runs is decided at run time, as for any case,
// so there is no finding (this was Unresolved before the runtime-item exemption)
// expect-none: LNT-018
module top(input logic a, input logic [1:0] s, output logic y);
  always_comb begin
    y = a;
    case (2'd1)
      s: y = ~a;
      default: y = 1'b0;
    endcase
  end
endmodule
