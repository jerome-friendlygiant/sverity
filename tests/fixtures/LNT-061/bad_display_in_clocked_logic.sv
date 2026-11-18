// sverity-args: --top top
// expect: LNT-061 WARNING 7
// revert: $display("state=%b", q); => ;
// revert-expect-none: LNT-061
module top(input logic clk,d, output logic q);
always @(posedge clk) begin
$display("state=%b", q);
q <= d;
end
endmodule
