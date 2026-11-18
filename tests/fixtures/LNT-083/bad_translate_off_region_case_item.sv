// sverity-args: --top top
// The region switches off the default item of a case: simulation has the item, synthesis builds a latch.
// expect: LNT-083 WARNING 10
module top(input logic a, b, input logic [1:0] s, output logic z);
  always_comb begin
    case (s)
      2'd0: z = a;
      2'd1: z = b;
      2'd2: z = a ^ b;
      // pragma translate_off
      default: z = 1'b0;
      // pragma translate_on
    endcase
  end
endmodule
