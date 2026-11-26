// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Trailing else-free `if (rst)` constant nonblocking overrides (with prior default and conditional assignments) are synchronous reset candidates: no RDC-025.
// expect-none: RDC-025
module top(input logic clka, rst, d, e, output logic o1, o2);
  reg f1 = 1'b0, f2 = 1'b0;
  reg [3:0] p = 0;
  reg [3:0] t;
  always @(posedge clka) begin
    f1 <= 1'b0;
    if (e) f1 <= d;
    if (rst) f1 <= 1'b0;
  end
  always @(posedge clka) begin
    f2 <= 1'b0;
    if (e) begin t = p + 1; p <= t; f2 <= d; end
    if (rst) begin p <= 0; f2 <= 1'b0; end
  end
  assign o1 = f1;
  assign o2 = f2;
endmodule
