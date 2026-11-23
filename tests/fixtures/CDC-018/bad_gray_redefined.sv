// sverity-args: --top top --sdc @DIR@/redefined_clock.sdc
// expect: CDC-018 WARNING
// expect-unresolved: CDC-018
module top(input logic clka,clkb,rst_n,inc,output logic [2:0] q);
logic [2:0] bin,gray,s1,s2;
always_ff @(posedge clka or negedge rst_n) if(!rst_n) begin bin<=0;gray<=0;end else begin bin<=bin+inc;gray<=bin^(bin>>1);end
always_ff @(posedge clkb or negedge rst_n) if(!rst_n) begin s1<=0;s2<=0;end else begin s1<=gray;s2<=s1;end
assign q=s2;endmodule
