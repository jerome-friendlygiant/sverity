// sverity-args: --top top
// `if (idx >= 4) break;` leaves the loop before the select for every index the array does not have: the select runs only for 0..3.
// expect-none: LNT-006
module top(input logic [7:0] d, input logic [2:0] idx, output logic y);
  logic [3:0] a;
  always_comb begin
    y = 1'b0;
    a = d[3:0];
    for (int i = 0; i < 2; i++) begin
      if (idx >= 3'd4) break;
      y = a[idx];
    end
  end
endmodule
