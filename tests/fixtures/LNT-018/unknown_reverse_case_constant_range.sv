// sverity-args: --top top
// A constant range in an inside-form case is not modelled, so which arms are reachable is not decided (Unresolved).
// expect: LNT-018 WARNING 7
module top(input logic is_a, output logic y);
  always_comb begin
    y = 1'b0;
    case (1'b1) inside
      [1'b0:1'b0]: y = 1'b1;
      is_a:        y = is_a;
      default:     y = 1'b0;
    endcase
  end
endmodule
