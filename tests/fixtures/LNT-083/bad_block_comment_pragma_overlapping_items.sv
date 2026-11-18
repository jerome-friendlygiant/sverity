// sverity-args: --top top
// The `/* */` spelling after the selector; two items match sel = 1, simulation takes the first, synthesis builds no priority.
// expect: LNT-083 WARNING 6
module top(input logic [1:0] sel, input logic a, b, output logic y);
  always_comb begin
    case (sel) /* synthesis parallel_case */
      2'b01: y = a;
      2'b01: y = b;
      default: y = 1'b0;
    endcase
  end
endmodule
