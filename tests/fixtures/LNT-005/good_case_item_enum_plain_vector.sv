// sverity-args: --top top
// A plain 2-bit selector against the constants of a 2-bit enum compares bit for bit. INFO, not a hazard.
// expect: LNT-005 INFO 11
// expect: LNT-005 INFO 12
// expect-none: LNT-005 WARNING
module top(input logic [1:0] mode, output logic y);
  typedef enum logic [1:0] {Idle = 2'd0, Run = 2'd1, Stop = 2'd2} mode_e;
  always_comb begin
    y = 1'b0;
    case (mode)
      Run: y = 1'b1;
      Stop: y = 1'b0;
      default: y = 1'b0;
    endcase
  end
endmodule
