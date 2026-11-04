module top(input logic a, b, d, output logic q);
  logic src, sync1;
  always_ff @(posedge a) src <= d;
  always_ff @(posedge b) begin
    sync1 <= src;
    q <= sync1;
  end
endmodule
