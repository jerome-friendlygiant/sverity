module case_ss02(input logic clk, input logic rst_n, input logic async_flag, output logic flag);
 logic [1:0] sync; always_ff @(posedge clk or negedge rst_n) if(!rst_n) sync<=0; else sync<={sync[0],async_flag};
 always_comb flag=sync[1];
endmodule
