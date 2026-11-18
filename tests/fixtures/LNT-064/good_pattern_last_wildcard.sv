// sverity-args: --top top
// A partly wildcard pattern, a guarded `.*` and a `.*` that is the last item shadow nothing.
// expect-none: LNT-064
module top(input logic [3:0] s, input logic en, output logic y);
  always_comb begin
    y = 1'b0;
    casez (s) matches
      4'b0???: y = 1'b1;
      default: y = 1'b0;
    endcase
    case (s) matches
      .v &&& en: y = v[0];
      4'b0001: y = 1'b0;
      .*: y = 1'b1;
    endcase
  end
endmodule
