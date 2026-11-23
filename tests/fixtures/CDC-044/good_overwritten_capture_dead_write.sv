// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Unconditionally overwritten capture write does not form an observable shift word.
// expect-none: CDC-044
module top(input logic clka, rx, output logic [1:0] word);
  logic s1, s2, s3;
  always_ff @(posedge clka) begin
    s1 <= rx;
    s2 <= s1;
    s3 <= s2;
    word <= {s3, s2};
    word <= 2'b00;
  end
endmodule
