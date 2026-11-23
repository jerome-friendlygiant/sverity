// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Competing constant sequential driver prevents claiming a single unconditional exact capture.
// expect-none: CDC-044
module top(input logic clka, rx, output logic [1:0] word);
  logic s1, s2, s3;
  always_ff @(posedge clka) begin
    s1 <= rx;
    s2 <= s1;
    s3 <= s2;
  end
  always @(posedge clka) word <= {s3, s2};
  always @(posedge clka) word <= 2'b00;
endmodule
