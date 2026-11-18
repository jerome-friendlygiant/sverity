// sverity-args: --top top
// `y` has a value before the case, but full_case tells synthesis the unlisted selector value cannot occur: its result is don't care,
// whatever was assigned before. One value is left, so the pragma still changes the logic.
// expect: LNT-083 WARNING 10
// revert: 2'd2: y = c; => 2'd2: y = c; 2'd3: y = 1'b0;
// revert-expect-none: LNT-083 WARNING
module top(input logic [1:0] s, input logic a, b, c, output logic y);
  always_comb begin
    y = 1'b0;
    case (s) // synopsys full_case
      2'd0: y = a;
      2'd1: y = b;
      2'd2: y = c;
    endcase
  end
endmodule
