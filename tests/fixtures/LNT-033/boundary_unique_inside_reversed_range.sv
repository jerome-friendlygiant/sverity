// sverity-args: --top top
// expect-none: LNT-033
// A range whose left bound exceeds the right one matches nothing (it is not swapped): [5:1] is empty, so [3:4] overlaps nothing.
module top(input logic [3:0] s, output logic y);
  always_comb begin
    y = 1'b0;
    unique case (s) inside
      [5:1]: y = 1'b1;
      [3:4]: y = 1'b0;
      default: y = 1'b0;
    endcase
  end
endmodule
