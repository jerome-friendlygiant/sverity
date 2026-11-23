// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-044 WARNING
module top(input logic clka, rst_n, rx, output logic [3:0] word, output logic hit);
  logic s1, s2, s3, s4;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) {s4, s3, s2, s1} <= '0;
    else {s4, s3, s2, s1} <= {s3, s2, s1, rx};
  always_ff @(posedge clka) word <= {s4, s3, s2, s1};
  assign hit = ({s4, s3, s2, s1} == 4'b0011);
endmodule
