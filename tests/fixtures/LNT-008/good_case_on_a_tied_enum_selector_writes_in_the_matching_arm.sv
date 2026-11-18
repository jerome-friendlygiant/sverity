// sverity-args: --top top
// The mode is an enum the only instance ties to its third value; the arm of that label writes r, the arms before it are dead.
// expect-none: LNT-008
package modes;
  typedef enum logic [2:0] { M1 = 3'b001, M2 = 3'b010, M3 = 3'b100 } mode_e;
endpackage
module child import modes::*; (input mode_e mode, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] r;
  always_comb begin
    unique case (mode)
      M1: r = d;
      M2: r = ~d;
      M3: r = d << 1;
      default: r = '0;
    endcase
  end
  assign q = r;
endmodule
module top import modes::*; (input logic [7:0] d, output logic [7:0] q);
  child u(.mode(M3), .d, .q);
endmodule
