// sverity-args: --top top
// expect-count: LNT-051 INFO 2
module top #(parameter int MODE = 1) (input logic a, output logic y);
  case (MODE)
    0: begin : c0
      assign y = a;
    end
    1: begin : c1
      assign y = ~a;
    end
    default: begin : cd
      assign y = 1'b0;
    end
  endcase
endmodule
