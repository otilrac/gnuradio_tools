module match_filter
   (input clk, input reset, input wire [15:0] r_input, input wire [15:0] i_input, 
    input rxstrobe, input wire [31:0] cdata,  input wire [2:0] cstate, input cwrite, 
    output wire [15:0] debugbus, output reg valid, output reg match);
  
    genvar  g;
    integer i;

    //setting up parameters  
    reg signed [15:0] threshhold;
    reg [2:0] co_len_state;
    reg [3:0] co_len_residual;
    always @(posedge clk)
        if(reset)
          begin
            threshhold      <= 0;
            co_len_state    <= 0;
            co_len_residual <= 0;
          end
        else if (in_state == 0)
          begin
            threshhold      <= cout_real;
            co_len_state    <= cout_img[6:4] + 3'd1;
            co_len_residual <= cout_img[3:0];
          end   

    //computation block
    reg signed [15:0] in_16_real   [15:0];
    reg signed [15:0] in_16_img    [15:0];

    wire [15:0] in_4_real          [3:0] ;
    wire [15:0] in_4_img           [3:0] ;
    wire [15:0] in_1_real                ;
    wire [15:0] in_1_img                 ;
    
    reg in_16_valid;
    reg in_4_valid;
    reg in_1_valid;


    generate for (g = 0; g < 4; g = g + 1)
      begin : generate_adder_4X24
      
        adder_4X16 a4_r_1(clk, reset, in_16_real[4*g], in_16_real[4*g+1], 
                          in_16_real[4*g+2], in_16_real[4*g+3], in_4_real[g]);

        adder_4X16 a4_i_1(clk, reset, in_16_img [4*g], in_16_img [4*g+1], 
                          in_16_img [4*g+2], in_16_img [4*g+3], in_4_img [g]);
      end
    endgenerate

    adder_4X16 a4_r_2(clk, reset, in_4_real[0], in_4_real[1], in_4_real[2],
                      in_4_real[3], in_1_real);
    adder_4X16 a4_i_2(clk, reset, in_4_img [0], in_4_img [1], in_4_img [2],
                      in_4_img [3], in_1_img );

    always @(posedge clk)
        if(reset)
          begin
            in_4_valid <= 0;
            in_1_valid <= 0;
          end
        else
          begin
            in_4_valid <= in_16_valid;
            in_1_valid <= in_4_valid;
          end

    //coefficient block
    wire [15:0] cout_real;
    wire [15:0] cout_img ;
    wire [2:0] ram_addr;

    assign ram_addr = (cwrite) ? cstate : in_state;
    true_dual_port_ram_single_clock ram
    (.data_a(cdata[31:16]), .data_b(cdata[15:0]), .addr_a({ram_addr, 1'b0}), 
     .addr_b({ram_addr, 1'b1}), .we_a(cwrite), .we_b(cwrite), .clk(clk), 
     .q_a(cout_real), .q_b(cout_img));   


    //data block
    
    wire [7:0] bridge_real[16:0];
    wire [7:0] bridge_img [16:0];
    wire [7:0] data_real[15:0];
    wire [7:0] data_img [15:0];
    wire [2:0]  sel;

    assign sel = in_state;
    assign bridge_real[0] = r_input[15:8];
    assign bridge_img[0]  = i_input[15:8];

    generate for (g = 0; g< 16; g = g + 1)
      begin : generate_shift_regs_real
      
        shift_register sr_r (.clk(clk), .reset(reset), .rxstrobe(rxstrobe),
                      .in_sample(bridge_real[g]), .out_sample(bridge_real[g+1]),
                      .sel(sel), .data(data_real[g]));
      end
    endgenerate

    generate for (g = 0; g< 16; g = g + 1)
      begin : generate_shift_regs_img
      
        shift_register sr_i (.clk(clk), .reset(reset), .rxstrobe(rxstrobe),
                      .in_sample(bridge_img[g]), .out_sample(bridge_img[g+1]),
                      .sel(sel), .data(data_img[g]));
      end
    endgenerate
    

    wire state_check_under;
    wire state_check_equal;
    assign state_check_under = in_state < co_len_state;
    assign state_check_equal = in_state == co_len_state; 
    
    //data selection block   
    always @ (posedge clk)
        if (reset)
          begin
            for (i = 0; i< 16; i = i + 1)
              begin
                in_16_real[i] <= 0;
                in_16_img [i] <= 0;
              end
          end
        else
          begin
            for (i = 0; i < 16; i = i + 1)
              begin
                if ((state_check_equal && i < co_len_residual) || 
                    state_check_under)
                  begin
                    in_16_real[i] <= (cout_real[i]) ? (data_real[i]) :
                                                      (-data_real[i]);
                    in_16_img [i] <= (cout_img[i] ) ? (data_img[i])  :
                                                      (-data_img[i]) ;
                  end
                else
                  begin
                    in_16_real[i] <= 0;
                    in_16_img [i] <= 0;
                  end
              end
          end
  
    //logic block
    reg [2:0]  in_state;
    reg signed [15:0] real_result; 
    reg signed [15:0] img_result;
    reg sum_valid;
    reg calculate;

    always @ (posedge clk)
        if (reset)
          begin
            in_state <= 0;
          end 
        else if (rxstrobe && !cwrite)
          begin
            in_state <= 3'd1;
          end  
        else if (in_state != 0)
          begin
            in_state <= in_state + 3'd1;
          end
        else
          begin
            in_state <= 0;
          end 
   
    always @ (posedge clk)
        if (reset)
            in_16_valid <= 0;
        else if (in_state > 3'd1)
            in_16_valid <= 1;
        else
            in_16_valid <= 0;

    always @ (posedge clk)
        if (reset)
          begin
            real_result <= 0;
            img_result  <= 0;
            sum_valid   <= 0;
          end
        else if (in_1_valid)
          begin
            real_result <= real_result + in_1_real;
            img_result  <= img_result  + in_1_img ;
            sum_valid   <= 0;
          end
        else if (sum_valid)
          begin
            real_result <= 0;
            img_result  <= 0;
            sum_valid   <= 0;
          end
        else if (real_result != 0 || img_result != 0)
          begin
            sum_valid   <= 1;
          end                

    reg signed [15:0] final_result;
    wire [15:0] real_result_abs;
    wire [15:0] img_result_abs;
    reg final_result_valid;
    
    assign real_result_abs = (real_result > 0) ? real_result : (-real_result);
    assign img_result_abs  = (img_result > 0)  ? img_result  : (-img_result) ;
 
    //final calculation
    always @ (posedge clk)
        if (reset)
          begin
            final_result <= 0;
            final_result_valid <= 0;
          end
        else if (sum_valid)
          begin
            if (real_result_abs > img_result_abs)
                final_result <= real_result_abs + {1'b0, img_result_abs[14:0]};
            else
                final_result <= {1'b0, real_result_abs[14:0]} + img_result_abs;
            final_result_valid <= 1;    
          end
        else
            final_result_valid <= 0; 

    //output evaluation
    always @ (posedge clk)
        if (reset)
          begin
            match <= 0;
            valid <= 0;
          end
        else if (final_result_valid)
          begin
            match <= (final_result > threshhold);
            valid <= 1'b1;
          end
        else
            valid <= 0;
   
    assign debugbus = {clk, match, valid, sum_valid, final_result_valid, 
                      in_16_valid, in_4_valid, in_1_valid, 
                      in_state[2:0], cstate[2:0], cwrite, cdata[0]};    
endmodule
